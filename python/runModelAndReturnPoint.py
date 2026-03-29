import argparse
from functools import lru_cache
from pathlib import Path
import sys

import numpy as np
import torch

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from gomoku_net import build_model_from_state_dict

EMPTY, BLACK, WHITE = 0, 1, 2
DIRECTIONS = ((1, 0), (0, 1), (1, 1), (1, -1))

DEFAULT_LOOKAHEAD_DEPTH = 2
LOOKAHEAD_MIN_STONES    = 6
LOOKAHEAD_TOP_ACTIONS   = 10
LOOKAHEAD_OPP_RESPONSES = 8

SYMBOL_TO_STONE = {".": EMPTY, "B": BLACK, "W": WHITE}
PLAYER_TO_STONE = {"B": BLACK, "W": WHITE}

opponent_of = {BLACK: WHITE, WHITE: BLACK}

# ── low-level line helpers ────────────────────────────────────────────────────

def _count_dir(grid, x, y, dx, dy, player, size):
    count = 0
    nx, ny = x + dx, y + dy
    while 0 <= nx < size and 0 <= ny < size and grid[nx, ny] == player:
        count += 1
        nx += dx; ny += dy
    return count

def _line_len(grid, x, y, player, dx, dy, size):
    return 1 + _count_dir(grid, x, y, -dx, -dy, player, size) \
        + _count_dir(grid, x, y,  dx,  dy, player, size)

def _open_ends(grid, x, y, player, dx, dy, size):
    left  = _count_dir(grid, x, y, -dx, -dy, player, size)
    right = _count_dir(grid, x, y,  dx,  dy, player, size)
    lx, ly = x - dx * (left + 1),  y - dy * (left + 1)
    rx, ry = x + dx * (right + 1), y + dy * (right + 1)
    return (int(0 <= lx < size and 0 <= ly < size and grid[lx, ly] == EMPTY) +
            int(0 <= rx < size and 0 <= ry < size and grid[rx, ry] == EMPTY))

# ── tactical score & winning detection ───────────────────────────────────────

# Score table: (line_len, open_ends) → score
_SCORE_TABLE = {
    (5, 0): 1_000_000, (5, 1): 1_000_000, (5, 2): 1_000_000,
    (4, 2):    60_000, (4, 1):    12_000,
    (3, 2):     1_500, (3, 1):       220,
    (2, 2):        80, (2, 1):        16,
}

def _tactical_score(grid, action, player, size):
    x, y = divmod(int(action), size)
    if grid[x, y] != EMPTY:
        return 0.0

    total = 0.0
    open_three = four_plus = 0
    for dx, dy in DIRECTIONS:
        ll = _line_len(grid, x, y, player, dx, dy, size)
        oe = _open_ends(grid, x, y, player, dx, dy, size)
        s  = _SCORE_TABLE.get((min(ll, 5), oe), 0)
        total += s
        if ll >= 4: four_plus  += 1
        if ll == 3 and oe == 2: open_three += 1

    if four_plus   >= 2: total += 90_000
    if open_three  >= 2: total +=  6_000
    return total

def _is_win(grid, action, player, size):
    x, y = divmod(int(action), size)
    return grid[x, y] == EMPTY and any(
        _line_len(grid, x, y, player, dx, dy, size) >= 5
        for dx, dy in DIRECTIONS
    )

def _winning_actions(grid, player, size):
    valids = np.flatnonzero(grid.reshape(-1) == EMPTY)
    return np.array([a for a in valids if _is_win(grid, a, player, size)], dtype=np.int32)

# ── candidate generation ──────────────────────────────────────────────────────

def _candidate_actions(grid, player, size, radius=2, max_actions=72):
    valids = np.flatnonzero(grid.reshape(-1) == EMPTY)
    if valids.size == 0:
        return valids.astype(np.int32)

    occupied = np.argwhere(grid != EMPTY)
    if occupied.size == 0:
        return np.array([(size // 2) * size + (size // 2)], dtype=np.int32)

    mask = np.zeros((size, size), dtype=bool)
    for ox, oy in occupied:
        mask[max(0, ox-radius):min(size, ox+radius+1),
        max(0, oy-radius):min(size, oy+radius+1)] = True
    mask &= (grid == EMPTY)

    candidates = np.flatnonzero(mask.reshape(-1))
    if candidates.size == 0:
        return valids.astype(np.int32)

    if 0 < max_actions < candidates.size:
        candidates = _top_scored(candidates, grid, player, size, max_actions)
    return candidates.astype(np.int32)

def _top_scored(actions, grid, player, size, limit):
    """Score and return top-`limit` actions by combined attack/defend/center."""
    opp    = opponent_of[player]
    center = (size - 1) * 0.5
    scores = np.array([
        _tactical_score(grid, a, player, size)
        + 0.92 * _tactical_score(grid, a, opp, size)
        + 0.15 * (size - (abs(divmod(int(a), size)[0] - center)
                          + abs(divmod(int(a), size)[1] - center)))
        for a in actions
    ])
    top = np.argpartition(scores, -limit)[-limit:]
    return actions[top[np.argsort(scores[top])[::-1]]]

def _top_actions(actions, scores, limit):
    if actions.size <= limit:
        return actions[np.argsort(scores)[::-1]].astype(np.int32)
    top = np.argpartition(scores, -limit)[-limit:]
    return actions[top[np.argsort(scores[top])[::-1]]].astype(np.int32)

# ── lookahead helpers ─────────────────────────────────────────────────────────

def _place(grid, action, player, size):
    x, y = divmod(int(action), size)
    if grid[x, y] != EMPTY:
        return None
    g = grid.copy(); g[x, y] = player
    return g

def _static_score(grid, player, size):
    opp = opponent_of[player]
    if _winning_actions(grid, player, size).size > 0: return  1_000_000.0
    if _winning_actions(grid, opp,    size).size > 0: return -1_000_000.0

    cands = _candidate_actions(grid, player, size, radius=2, max_actions=48)
    if cands.size == 0:
        cands = np.flatnonzero(grid.reshape(-1) == EMPTY).astype(np.int32)
    if cands.size == 0:
        return 0.0

    my_best  = max(_tactical_score(grid, a, player, size) for a in cands)
    opp_best = max(_tactical_score(grid, a, opp,    size) for a in cands)
    return float(my_best - 0.88 * opp_best)

def _opp_responses(grid, opp, size, limit):
    wins = _winning_actions(grid, opp, size)
    if wins.size > 0:
        return wins[:limit]
    cands = _candidate_actions(grid, opp, size, radius=2, max_actions=max(24, limit * 3))
    if cands.size == 0:
        cands = np.flatnonzero(grid.reshape(-1) == EMPTY).astype(np.int32)
    if cands.size == 0:
        return cands

    player = opponent_of[opp]
    scores = np.array([
        _tactical_score(grid, a, opp, size) + 0.95 * _tactical_score(grid, a, player, size)
        for a in cands
    ])
    return _top_actions(cands, scores, limit)

def _evaluate_lookahead(grid, action, player, size, depth, resp_limit):
    next_g = _place(grid, action, player, size)
    if next_g is None:
        return -1_000_000_000.0

    opp = opponent_of[player]
    if _winning_actions(next_g, opp, size).size > 0:
        return -900_000.0

    if depth <= 1:
        return _static_score(next_g, player, size)

    responses = _opp_responses(next_g, opp, size, resp_limit)
    if responses.size == 0:
        return _static_score(next_g, player, size)

    worst = float("inf")
    for resp in responses:
        if _is_win(next_g, resp, opp, size):
            return -950_000.0
        rg = _place(next_g, resp, opp, size)
        if rg is None:
            continue
        score = (700_000.0 if _winning_actions(rg, player, size).size > 0
                 else _static_score(rg, player, size))
        worst = min(worst, score)

    return _static_score(next_g, player, size) if not np.isfinite(worst) else float(worst)

# ── board / model utilities ───────────────────────────────────────────────────

def parse_board(board_text, size):
    text = str(board_text)
    rows = text.split("|") if "|" in text else [text[i*size:(i+1)*size] for i in range(size)]
    if len(rows) != size:
        raise ValueError(f"board row count mismatch: expected {size}, got {len(rows)}")
    grid = np.zeros((size, size), dtype=np.int8)
    for y, row in enumerate(rows):
        if len(row) != size:
            raise ValueError(f"row {y} width mismatch")
        for x, sym in enumerate(row):
            if sym not in SYMBOL_TO_STONE:
                raise ValueError(f"invalid symbol '{sym}' at ({x},{y})")
            grid[x, y] = SYMBOL_TO_STONE[sym]
    return grid

def build_observation(grid, player):
    opp = opponent_of[player]
    obs = np.empty((3, *grid.shape), dtype=np.float32)
    obs[0] = (grid == player).astype(np.float32)
    obs[1] = (grid == opp   ).astype(np.float32)
    obs[2] = (grid == EMPTY ).astype(np.float32)
    return obs

@lru_cache(maxsize=4)
def load_model(model_path, size):
    state = torch.load(Path(model_path), map_location="cpu")
    model, _ = build_model_from_state_dict(state, board_size=size)
    return model.eval()

def _resolve_model(path):
    p = Path(path)
    if p.is_absolute():
        return p
    for candidate in (Path.cwd() / p, Path(__file__).resolve().parent / p, PROJECT_ROOT / p):
        if candidate.exists():
            return candidate
    return p

def _normalize(values, mask):
    out = np.zeros_like(values, dtype=np.float64)
    v = values[mask]
    lo, hi = v.min(), v.max()
    if hi - lo > 1e-12:
        out[mask] = (v - lo) / (hi - lo)
    return out

def _blend_scores(arrays_weights, mask):
    """Weighted sum of normalized score arrays over `mask`."""
    return sum(w * _normalize(arr, mask) for arr, w in arrays_weights)

# ── main AI logic ─────────────────────────────────────────────────────────────

def getAIMove(board_text, current_player, model_path="gomoku_model.pt",
              board_size=15, lookahead_depth=DEFAULT_LOOKAHEAD_DEPTH):
    key = current_player.upper()
    if key not in PLAYER_TO_STONE:
        raise ValueError("current_player must be B or W")

    grid   = parse_board(board_text, board_size)
    player = PLAYER_TO_STONE[key]
    opp    = opponent_of[player]
    size   = board_size

    model_file = _resolve_model(model_path)
    if not model_file.exists():
        raise FileNotFoundError(f"model not found: {model_file}")

    # Model inference
    obs = build_observation(grid, player)
    with torch.inference_mode():
        policy, _ = load_model(str(model_file.resolve()), size)(
            torch.from_numpy(obs).unsqueeze(0).float()
        )
    logits = policy.squeeze(0).cpu().numpy().astype(np.float64)
    logits = np.nan_to_num(logits, nan=0.0, posinf=0.0, neginf=0.0)

    valid_mask = grid.reshape(-1) == EMPTY
    if not valid_mask.any():
        raise ValueError("no valid moves")

    # Tactical guardrails
    for p, mask_p in ((player, True), (opp, False)):
        wins = _winning_actions(grid, p, size)
        if wins.size > 0:
            best = int(wins[np.argmax(logits[wins])])
            return divmod(best, size)

    # Candidate mask
    cands = _candidate_actions(grid, player, size, radius=2, max_actions=72)
    allowed = np.zeros(size * size, dtype=bool)
    if cands.size > 0:
        allowed[cands] = True
    allowed &= valid_mask
    if not allowed.any():
        allowed = valid_mask.copy()

    # Policy probabilities (softmax over allowed)
    masked_logits = np.where(allowed, logits, -np.inf)
    shifted = masked_logits[allowed] - masked_logits[allowed].max()
    exp = np.exp(shifted)
    policy_probs = np.zeros(size * size, dtype=np.float64)
    denom = exp.sum()
    policy_probs[allowed] = exp / denom if denom > 1e-12 else 1.0 / allowed.sum()

    # Tactical scores
    allowed_idx = np.flatnonzero(allowed)
    center      = (size - 1) * 0.5
    attack  = np.zeros(size * size, dtype=np.float64)
    defend  = np.zeros(size * size, dtype=np.float64)
    c_bias  = np.zeros(size * size, dtype=np.float64)
    for a in allowed_idx:
        attack[a] = _tactical_score(grid, a, player, size)
        defend[a] = _tactical_score(grid, a, opp,    size)
        x, y      = divmod(int(a), size)
        c_bias[a] = size - (abs(x - center) + abs(y - center))

    move_scores = _blend_scores([
        (policy_probs, 0.62),
        (attack,       0.23),
        (defend,       0.12),
        (c_bias,       0.03),
    ], allowed)

    # Optional lookahead
    stone_count = int((grid != EMPTY).sum())
    if lookahead_depth > 0 and stone_count >= LOOKAHEAD_MIN_STONES and allowed.sum() > 1:
        top_n    = min(LOOKAHEAD_TOP_ACTIONS, allowed_idx.size)
        top_acts = allowed_idx[np.argsort(move_scores[allowed_idx])[::-1]][:top_n].astype(np.int32)

        la_scores = np.full(size * size, -np.inf, dtype=np.float64)
        for a in top_acts:
            la_scores[a] = _evaluate_lookahead(grid, a, player, size,
                                               lookahead_depth, LOOKAHEAD_OPP_RESPONSES)

        top_mask = np.zeros_like(allowed, dtype=bool)
        top_mask[top_acts] = True

        base_n = _normalize(move_scores, allowed)
        la_n   = _normalize(la_scores,   top_mask)
        move_scores = base_n.copy()
        move_scores[top_mask] = 0.72 * base_n[top_mask] + 0.28 * la_n[top_mask]

    move_scores[~allowed] = -np.inf
    return divmod(int(np.argmax(move_scores)), size)


def main():
    parser = argparse.ArgumentParser(description="Return AI move from gomoku_model.pt")
    parser.add_argument("--board",          required=True)
    parser.add_argument("--current",        required=True)
    parser.add_argument("--board-size",     type=int, default=15)
    parser.add_argument("--model-path",     default="gomoku_model.pt")
    parser.add_argument("--lookahead-depth",type=int, default=DEFAULT_LOOKAHEAD_DEPTH)
    args = parser.parse_args()

    try:
        x, y = getAIMove(args.board, args.current, args.model_path,
                         args.board_size, args.lookahead_depth)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"{x} {y}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())