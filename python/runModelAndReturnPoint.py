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

EMPTY = 0
BLACK = 1
WHITE = 2
DIRECTIONS = ((1, 0), (0, 1), (1, 1), (1, -1))

DEFAULT_LOOKAHEAD_DEPTH = 2
LOOKAHEAD_MIN_STONES = 6
LOOKAHEAD_TOP_ACTIONS = 10
LOOKAHEAD_OPP_RESPONSES = 8

SYMBOL_TO_STONE = {
    ".": EMPTY,
    "B": BLACK,
    "W": WHITE,
}

PLAYER_TO_STONE = {
    "B": BLACK,
    "W": WHITE,
}


def _count_direction(grid: np.ndarray, x: int, y: int, dx: int, dy: int, player: int, board_size: int) -> int:
    count = 0
    step = 1
    while step < 5:
        nx = x + dx * step
        ny = y + dy * step
        if nx < 0 or nx >= board_size or ny < 0 or ny >= board_size:
            break
        if int(grid[nx, ny]) != player:
            break
        count += 1
        step += 1
    return count


def _line_length_if_place(grid: np.ndarray, x: int, y: int, player: int, dx: int, dy: int, board_size: int) -> int:
    left = _count_direction(grid, x, y, -dx, -dy, player, board_size)
    right = _count_direction(grid, x, y, dx, dy, player, board_size)
    return 1 + left + right


def _open_ends_if_place(grid: np.ndarray, x: int, y: int, player: int, dx: int, dy: int, board_size: int) -> int:
    left = _count_direction(grid, x, y, -dx, -dy, player, board_size)
    right = _count_direction(grid, x, y, dx, dy, player, board_size)

    lx = x - dx * (left + 1)
    ly = y - dy * (left + 1)
    rx = x + dx * (right + 1)
    ry = y + dy * (right + 1)

    open_ends = 0
    if 0 <= lx < board_size and 0 <= ly < board_size and int(grid[lx, ly]) == EMPTY:
        open_ends += 1
    if 0 <= rx < board_size and 0 <= ry < board_size and int(grid[rx, ry]) == EMPTY:
        open_ends += 1
    return open_ends


def _is_winning_action(grid: np.ndarray, action: int, player: int, board_size: int) -> bool:
    x, y = divmod(int(action), board_size)
    if int(grid[x, y]) != EMPTY:
        return False

    for dx, dy in DIRECTIONS:
        if _line_length_if_place(grid, x, y, player, dx, dy, board_size) >= 5:
            return True
    return False


def _winning_actions(grid: np.ndarray, player: int, board_size: int) -> np.ndarray:
    valid_actions = np.flatnonzero(grid.reshape(-1) == EMPTY)
    if valid_actions.size == 0:
        return np.asarray([], dtype=np.int32)

    wins = [int(a) for a in valid_actions if _is_winning_action(grid, int(a), player, board_size)]
    return np.asarray(wins, dtype=np.int32)


def _tactical_score(grid: np.ndarray, action: int, player: int, board_size: int) -> float:
    x, y = divmod(int(action), board_size)
    if int(grid[x, y]) != EMPTY:
        return 0.0

    score = 0.0
    open_three_count = 0
    four_plus_count = 0
    for dx, dy in DIRECTIONS:
        line_len = _line_length_if_place(grid, x, y, player, dx, dy, board_size)
        if line_len >= 5:
            return 1_000_000.0

        open_ends = _open_ends_if_place(grid, x, y, player, dx, dy, board_size)
        if line_len == 4:
            if open_ends == 2:
                score += 60_000.0
                four_plus_count += 1
            elif open_ends == 1:
                score += 12_000.0
                four_plus_count += 1
        elif line_len == 3:
            if open_ends == 2:
                score += 1_500.0
                open_three_count += 1
            elif open_ends == 1:
                score += 220.0
        elif line_len == 2:
            if open_ends == 2:
                score += 80.0
            elif open_ends == 1:
                score += 16.0

    if four_plus_count >= 2:
        score += 90_000.0
    if open_three_count >= 2:
        score += 6_000.0
    return score


def _candidate_actions(
    grid: np.ndarray,
    current_player: int,
    board_size: int,
    radius: int = 2,
    max_actions: int = 72,
) -> np.ndarray:
    valid_actions = np.flatnonzero(grid.reshape(-1) == EMPTY)
    if valid_actions.size == 0:
        return np.asarray([], dtype=np.int32)

    occupied = np.argwhere(grid != EMPTY)
    if occupied.size == 0:
        center = (board_size // 2) * board_size + (board_size // 2)
        return np.asarray([center], dtype=np.int32)

    radius = max(1, int(radius))
    mask = np.zeros((board_size, board_size), dtype=bool)
    for ox, oy in occupied:
        x0 = max(0, int(ox) - radius)
        x1 = min(board_size, int(ox) + radius + 1)
        y0 = max(0, int(oy) - radius)
        y1 = min(board_size, int(oy) + radius + 1)
        mask[x0:x1, y0:y1] = True

    mask &= (grid == EMPTY)
    candidates = np.flatnonzero(mask.reshape(-1))
    if candidates.size == 0:
        return valid_actions.astype(np.int32)

    max_actions = int(max_actions)
    if 0 < max_actions < candidates.size:
        opponent = WHITE if current_player == BLACK else BLACK
        center = (board_size - 1) * 0.5
        scores = np.zeros(candidates.size, dtype=np.float64)
        for i, action in enumerate(candidates):
            x, y = divmod(int(action), board_size)
            attack = _tactical_score(grid, int(action), current_player, board_size)
            defend = _tactical_score(grid, int(action), opponent, board_size)
            center_bias = board_size - (abs(x - center) + abs(y - center))
            scores[i] = attack + 0.92 * defend + 0.15 * center_bias

        top_idx = np.argpartition(scores, -max_actions)[-max_actions:]
        top_scores = scores[top_idx]
        order = np.argsort(top_scores)[::-1]
        candidates = candidates[top_idx[order]]

    return candidates.astype(np.int32)


def _minmax_normalize(values: np.ndarray, mask: np.ndarray) -> np.ndarray:
    out = np.zeros_like(values, dtype=np.float64)
    masked_values = values[mask]
    if masked_values.size == 0:
        return out

    min_v = float(np.min(masked_values))
    max_v = float(np.max(masked_values))
    if max_v - min_v <= 1e-12:
        return out
    out[mask] = (masked_values - min_v) / (max_v - min_v)
    return out


def _place_action(grid: np.ndarray, action: int, player: int, board_size: int) -> np.ndarray | None:
    x, y = divmod(int(action), board_size)
    if int(grid[x, y]) != EMPTY:
        return None
    next_grid = grid.copy()
    next_grid[x, y] = player
    return next_grid


def _static_position_score(grid: np.ndarray, player: int, board_size: int) -> float:
    opponent = WHITE if player == BLACK else BLACK

    winning = _winning_actions(grid, player, board_size)
    if winning.size > 0:
        return 1_000_000.0

    blocking = _winning_actions(grid, opponent, board_size)
    if blocking.size > 0:
        return -1_000_000.0

    candidates = _candidate_actions(
        grid,
        current_player=player,
        board_size=board_size,
        radius=2,
        max_actions=48,
    )
    if candidates.size == 0:
        candidates = np.flatnonzero(grid.reshape(-1) == EMPTY).astype(np.int32)
    if candidates.size == 0:
        return 0.0

    my_best_attack = 0.0
    opp_best_attack = 0.0
    for action in candidates:
        my_best_attack = max(my_best_attack, _tactical_score(grid, int(action), player, board_size))
        opp_best_attack = max(opp_best_attack, _tactical_score(grid, int(action), opponent, board_size))

    return float(my_best_attack - 0.88 * opp_best_attack)


def _top_actions(actions: np.ndarray, scores: np.ndarray, limit: int) -> np.ndarray:
    if actions.size == 0:
        return np.asarray([], dtype=np.int32)
    if actions.size <= int(limit):
        order = np.argsort(scores)[::-1]
        return actions[order].astype(np.int32)

    top_idx = np.argpartition(scores, -int(limit))[-int(limit):]
    top_scores = scores[top_idx]
    order = np.argsort(top_scores)[::-1]
    return actions[top_idx[order]].astype(np.int32)


def _select_opponent_responses(
    grid: np.ndarray,
    opponent: int,
    board_size: int,
    limit: int,
) -> np.ndarray:
    limit = max(1, int(limit))
    winning = _winning_actions(grid, opponent, board_size)
    if winning.size > 0:
        return winning[:limit]

    candidates = _candidate_actions(
        grid,
        current_player=opponent,
        board_size=board_size,
        radius=2,
        max_actions=max(24, limit * 3),
    )
    if candidates.size == 0:
        candidates = np.flatnonzero(grid.reshape(-1) == EMPTY).astype(np.int32)
    if candidates.size == 0:
        return np.asarray([], dtype=np.int32)

    player = WHITE if opponent == BLACK else BLACK
    scores = np.zeros(candidates.size, dtype=np.float64)
    for i, action in enumerate(candidates):
        attack = _tactical_score(grid, int(action), opponent, board_size)
        deny = _tactical_score(grid, int(action), player, board_size)
        scores[i] = attack + 0.95 * deny

    return _top_actions(candidates, scores, limit=limit)


def _evaluate_move_with_lookahead(
    grid: np.ndarray,
    action: int,
    player: int,
    board_size: int,
    depth: int,
    response_limit: int,
) -> float:
    next_grid = _place_action(grid, int(action), player, board_size)
    if next_grid is None:
        return -1_000_000_000.0

    opponent = WHITE if player == BLACK else BLACK
    opp_winning = _winning_actions(next_grid, opponent, board_size)
    if opp_winning.size > 0:
        return -900_000.0 - float(opp_winning.size)

    if int(depth) <= 1:
        return _static_position_score(next_grid, player, board_size)

    responses = _select_opponent_responses(
        next_grid,
        opponent=opponent,
        board_size=board_size,
        limit=response_limit,
    )
    if responses.size == 0:
        return _static_position_score(next_grid, player, board_size)

    worst_case = float("inf")
    for response in responses:
        response_grid = _place_action(next_grid, int(response), opponent, board_size)
        if response_grid is None:
            continue

        if _is_winning_action(next_grid, int(response), opponent, board_size):
            return -950_000.0

        my_winning = _winning_actions(response_grid, player, board_size)
        if my_winning.size > 0:
            score = 700_000.0
        else:
            score = _static_position_score(response_grid, player, board_size)

        if score < worst_case:
            worst_case = score

    if not np.isfinite(worst_case):
        return _static_position_score(next_grid, player, board_size)
    return float(worst_case)


def parse_board(board_text: str, board_size: int) -> np.ndarray:
    text = str(board_text)
    if "|" in text:
        rows = text.split("|")
        if len(rows) != board_size:
            raise ValueError(f"board row count mismatch: expected {board_size}, got {len(rows)}")
    else:
        expected = board_size * board_size
        if len(text) != expected:
            raise ValueError(
                f"board text length mismatch: expected {expected}, got {len(text)}"
            )
        rows = [text[i * board_size:(i + 1) * board_size] for i in range(board_size)]

    # Keep the same indexing as the C++ binding: grid[x, y].
    grid = np.zeros((board_size, board_size), dtype=np.int8)
    for y, row in enumerate(rows):
        if len(row) != board_size:
            raise ValueError(f"board col count mismatch in row {y}: expected {board_size}, got {len(row)}")
        for x, symbol in enumerate(row):
            if symbol not in SYMBOL_TO_STONE:
                raise ValueError(f"invalid board symbol '{symbol}' at ({x}, {y})")
            grid[x, y] = SYMBOL_TO_STONE[symbol]

    return grid


def build_observation(grid: np.ndarray, current_player: int) -> np.ndarray:
    opponent = WHITE if current_player == BLACK else BLACK

    obs = np.empty((3, grid.shape[0], grid.shape[1]), dtype=np.float32)
    obs[0] = (grid == current_player).astype(np.float32)
    obs[1] = (grid == opponent).astype(np.float32)
    obs[2] = (grid == EMPTY).astype(np.float32)
    return obs


@lru_cache(maxsize=4)
def load_model(model_path: str, board_size: int) -> torch.nn.Module:
    model_file = Path(model_path)
    state_dict = torch.load(model_file, map_location="cpu")
    model, _ = build_model_from_state_dict(state_dict, board_size=board_size)
    model.eval()
    return model


def _resolve_model_path(model_path: str) -> Path:
    model_file = Path(model_path)
    if model_file.is_absolute():
        return model_file

    # Preserve old behavior and support moved scripts:
    # 1) current working directory, 2) script dir, 3) project root.
    candidates = (
        Path.cwd() / model_file,
        Path(__file__).resolve().parent / model_file,
        PROJECT_ROOT / model_file,
    )
    for candidate in candidates:
        if candidate.exists():
            return candidate

    return model_file


def getAIMove(
    board_text: str,
    current_player: str,
    model_path: str = "gomoku_model.pt",
    board_size: int = 15,
    lookahead_depth: int = DEFAULT_LOOKAHEAD_DEPTH,
):
    player_key = current_player.upper()
    if player_key not in PLAYER_TO_STONE:
        raise ValueError("current player must be B or W")

    grid = parse_board(board_text, board_size)
    player = PLAYER_TO_STONE[player_key]

    model_file = _resolve_model_path(model_path)
    if not model_file.exists():
        raise FileNotFoundError(f"model file not found: {model_file}")

    model = load_model(str(model_file.resolve()), board_size)

    obs = build_observation(grid, player)
    tensor_input = torch.from_numpy(obs).unsqueeze(0).float()

    with torch.inference_mode():
        policy, _ = model(tensor_input)

    move_logits = policy.squeeze(0).cpu().numpy().astype(np.float64)
    if not np.all(np.isfinite(move_logits)):
        move_logits = np.nan_to_num(move_logits, nan=0.0, posinf=0.0, neginf=0.0)
    valid_mask = (grid.reshape(-1) == EMPTY)
    if not np.any(valid_mask):
        raise ValueError("no valid moves")

    opponent = WHITE if player == BLACK else BLACK

    # Tactical guardrails: always take immediate wins, always block opponent immediate wins.
    winning = _winning_actions(grid, player, board_size)
    if winning.size > 0:
        best_move = int(winning[np.argmax(move_logits[winning])])
        x, y = divmod(best_move, board_size)
        return x, y

    blocking = _winning_actions(grid, opponent, board_size)
    if blocking.size > 0:
        best_move = int(blocking[np.argmax(move_logits[blocking])])
        x, y = divmod(best_move, board_size)
        return x, y

    allowed_mask = np.zeros(board_size * board_size, dtype=bool)
    candidates = _candidate_actions(
        grid,
        current_player=player,
        board_size=board_size,
        radius=2,
        max_actions=72,
    )
    if candidates.size > 0:
        allowed_mask[candidates] = True
    allowed_mask &= valid_mask
    if not np.any(allowed_mask):
        allowed_mask = valid_mask.copy()

    logits = move_logits.copy()
    logits[~allowed_mask] = -np.inf

    finite_logits = logits[allowed_mask]
    if finite_logits.size == 0:
        best_move = int(np.flatnonzero(valid_mask)[0])
        x, y = divmod(best_move, board_size)
        return x, y

    shifted = finite_logits - np.max(finite_logits)
    exp_logits = np.exp(shifted)
    policy_probs = np.zeros_like(logits, dtype=np.float64)
    denom = float(np.sum(exp_logits))
    if denom > 1e-12:
        policy_probs[allowed_mask] = exp_logits / denom
    else:
        policy_probs[allowed_mask] = 1.0 / float(np.sum(allowed_mask))

    attack_scores = np.zeros(board_size * board_size, dtype=np.float64)
    defend_scores = np.zeros(board_size * board_size, dtype=np.float64)
    center_bias = np.zeros(board_size * board_size, dtype=np.float64)
    center = (board_size - 1) * 0.5
    for action in np.flatnonzero(allowed_mask):
        attack_scores[action] = _tactical_score(grid, int(action), player, board_size)
        defend_scores[action] = _tactical_score(grid, int(action), opponent, board_size)
        x, y = divmod(int(action), board_size)
        center_bias[action] = board_size - (abs(x - center) + abs(y - center))

    policy_norm = _minmax_normalize(policy_probs, allowed_mask)
    attack_norm = _minmax_normalize(attack_scores, allowed_mask)
    defend_norm = _minmax_normalize(defend_scores, allowed_mask)
    center_norm = _minmax_normalize(center_bias, allowed_mask)

    move_scores = (
        0.62 * policy_norm
        + 0.23 * attack_norm
        + 0.12 * defend_norm
        + 0.03 * center_norm
    )

    stone_count = int(np.count_nonzero(grid != EMPTY))
    lookahead_depth = max(0, int(lookahead_depth))
    if (
        lookahead_depth > 0
        and stone_count >= LOOKAHEAD_MIN_STONES
        and np.sum(allowed_mask) > 1
    ):
        allowed_actions = np.flatnonzero(allowed_mask)
        top_count = min(LOOKAHEAD_TOP_ACTIONS, allowed_actions.size)
        ranked = allowed_actions[np.argsort(move_scores[allowed_actions])[::-1]]
        top_actions = ranked[:top_count].astype(np.int32)

        lookahead_scores = np.full(board_size * board_size, -np.inf, dtype=np.float64)
        for action in top_actions:
            lookahead_scores[int(action)] = _evaluate_move_with_lookahead(
                grid,
                action=int(action),
                player=player,
                board_size=board_size,
                depth=lookahead_depth,
                response_limit=LOOKAHEAD_OPP_RESPONSES,
            )

        top_mask = np.zeros_like(allowed_mask, dtype=bool)
        top_mask[top_actions] = True

        base_norm = _minmax_normalize(move_scores, allowed_mask)
        lookahead_norm = _minmax_normalize(lookahead_scores, top_mask)
        blended = base_norm.copy()
        blended[top_mask] = 0.72 * base_norm[top_mask] + 0.28 * lookahead_norm[top_mask]
        move_scores = blended

    move_scores[~allowed_mask] = -np.inf
    best_move = int(np.argmax(move_scores))
    x, y = divmod(best_move, board_size)
    return x, y


def main() -> int:
    parser = argparse.ArgumentParser(description="Return AI move from gomoku_model.pt")
    parser.add_argument(
        "--board",
        required=True,
        help="serialized board, either rows joined by '|' or flat length board_size^2, using . B W",
    )
    parser.add_argument("--current", required=True, help="current player, B or W")
    parser.add_argument("--board-size", type=int, default=15)
    parser.add_argument("--model-path", default="gomoku_model.pt")
    parser.add_argument("--lookahead-depth", type=int, default=DEFAULT_LOOKAHEAD_DEPTH)
    args = parser.parse_args()

    try:
        x, y = getAIMove(
            board_text=args.board,
            current_player=args.current,
            model_path=args.model_path,
            board_size=args.board_size,
            lookahead_depth=args.lookahead_depth,
        )
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"{x} {y}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
