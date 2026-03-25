import math

import numpy as np
import torch

import gomoku_ai


EMPTY = 0
BLACK = 1
WHITE = 2

STATUS_PLAYING = 0
STATUS_BLACK_WIN = 1
STATUS_WHITE_WIN = 2
STATUS_DRAW = 3


def _stone_to_int(stone):
    if stone == gomoku_ai.Stone.BLACK:
        return BLACK
    if stone == gomoku_ai.Stone.WHITE:
        return WHITE
    return EMPTY


def _status_to_int(status):
    if status == gomoku_ai.GameStatus.BLACK_WIN:
        return STATUS_BLACK_WIN
    if status == gomoku_ai.GameStatus.WHITE_WIN:
        return STATUS_WHITE_WIN
    if status == gomoku_ai.GameStatus.DRAW:
        return STATUS_DRAW
    return STATUS_PLAYING


class _BoardState:
    def __init__(self, size, grid, current_player, status):
        self.size = int(size)
        self.grid = grid
        self.current_player = int(current_player)
        self.status = int(status)

    @classmethod
    def from_cpp_board(cls, board):
        obs = board.get_observation()
        size = int(obs.shape[1])
        grid = np.zeros((size, size), dtype=np.int8)
        for x in range(size):
            for y in range(size):
                grid[x, y] = _stone_to_int(board.get_stone(x, y))
        return cls(
            size=size,
            grid=grid,
            current_player=_stone_to_int(board.get_current_player()),
            status=_status_to_int(board.get_status()),
        )

    def clone(self):
        return _BoardState(
            size=self.size,
            grid=self.grid.copy(),
            current_player=self.current_player,
            status=self.status,
        )

    def get_valid_moves(self):
        if self.status != STATUS_PLAYING:
            return np.zeros(self.size * self.size, dtype=np.float32)
        return (self.grid.reshape(-1) == EMPTY).astype(np.float32)

    def get_observation(self):
        me = self.current_player
        opp = WHITE if me == BLACK else BLACK
        obs = np.empty((3, self.size, self.size), dtype=np.float32)
        obs[0] = (self.grid == me).astype(np.float32)
        obs[1] = (self.grid == opp).astype(np.float32)
        obs[2] = (self.grid == EMPTY).astype(np.float32)
        return obs

    def place(self, action):
        if self.status != STATUS_PLAYING:
            return False

        x, y = divmod(int(action), self.size)
        if x < 0 or x >= self.size or y < 0 or y >= self.size:
            return False
        if self.grid[x, y] != EMPTY:
            return False

        player = self.current_player
        self.grid[x, y] = player

        if self._check_win(x, y):
            self.status = STATUS_BLACK_WIN if player == BLACK else STATUS_WHITE_WIN
            return True

        if not np.any(self.grid == EMPTY):
            self.status = STATUS_DRAW
            return True

        self.current_player = WHITE if player == BLACK else BLACK
        return True

    def terminal_value(self):
        if self.status == STATUS_DRAW:
            return 0.0
        return -1.0

    def _check_win(self, last_x, last_y):
        target = self.grid[last_x, last_y]
        if target == EMPTY:
            return False

        directions = ((1, 0), (0, 1), (1, 1), (1, -1))

        for dx, dy in directions:
            count = 1

            step = 1
            while step < 5:
                x = last_x + dx * step
                y = last_y + dy * step
                if x < 0 or x >= self.size or y < 0 or y >= self.size:
                    break
                if self.grid[x, y] != target:
                    break
                count += 1
                step += 1

            step = 1
            while step < 5:
                x = last_x - dx * step
                y = last_y - dy * step
                if x < 0 or x >= self.size or y < 0 or y >= self.size:
                    break
                if self.grid[x, y] != target:
                    break
                count += 1
                step += 1

            if count >= 5:
                return True

        return False


class TreeNode:
    def __init__(self, parent, prior_p):
        self.parent = parent
        self.children = {}
        self.n_visits = 0
        self.Q = 0.0
        self.P = float(prior_p)

    def is_leaf(self):
        return len(self.children) == 0

    def isleaf(self):
        return self.is_leaf()

    def is_root(self):
        return self.parent is None

    def isroot(self):
        return self.is_root()

    def expand(self, action_priors):
        for action, prior in action_priors:
            if action not in self.children:
                self.children[action] = TreeNode(self, float(prior))

    def select(self, cpuct):
        return max(
            self.children.items(),
            key=lambda item: item[1].get_value(cpuct),
        )

    def get_value(self, cpuct):
        parent_visits = self.parent.n_visits if self.parent is not None else 1
        u = cpuct * self.P * math.sqrt(parent_visits) / (1 + self.n_visits)
        return self.Q + u

    def update(self, leaf_value):
        self.n_visits += 1
        self.Q += (leaf_value - self.Q) / self.n_visits

    def update_recursive(self, leaf_value):
        if self.parent is not None:
            self.parent.update_recursive(-leaf_value)
        self.update(leaf_value)


class MCTS:
    def __init__(self, model, simulations=1000, cpuct=5.0):
        self.model = model
        self.simulations = int(simulations)
        self.cpuct = float(cpuct)
        self._device = self._resolve_device()

    def _resolve_device(self):
        try:
            return next(self.model.parameters()).device
        except (AttributeError, StopIteration, TypeError):
            return torch.device("cpu")

    def _policy_value(self, state):
        obs = torch.from_numpy(state.get_observation()).float().unsqueeze(0).to(self._device)
        with torch.no_grad():
            policy, value = self.model(obs)

        policy = np.asarray(policy.detach().cpu().numpy()).reshape(-1)
        value = float(np.asarray(value.detach().cpu().numpy()).reshape(-1)[0])

        expected_size = state.size * state.size
        if policy.size != expected_size:
            raise ValueError(
                f"Policy size mismatch: expected {expected_size}, got {policy.size}"
            )
        return policy, value

    @staticmethod
    def _normalize_policy(policy, valid_moves):
        policy = np.asarray(policy, dtype=np.float64)
        valid_moves = np.asarray(valid_moves, dtype=np.float64)

        policy = np.clip(policy, a_min=0.0, a_max=None)
        masked = policy * valid_moves
        total = masked.sum()

        if total <= 1e-12:
            count = valid_moves.sum()
            if count <= 0:
                return valid_moves
            return valid_moves / count

        return masked / total

    def _run_simulation(self, root, root_state):
        node = root
        state = root_state.clone()

        while not node.is_leaf() and state.status == STATUS_PLAYING:
            action, next_node = node.select(self.cpuct)
            if not state.place(action):
                break
            node = next_node

        if state.status == STATUS_PLAYING:
            policy, leaf_value = self._policy_value(state)
            valid_moves = state.get_valid_moves()
            policy = self._normalize_policy(policy, valid_moves)
            action_priors = [(int(a), float(policy[a])) for a in np.flatnonzero(valid_moves)]
            node.expand(action_priors)
        else:
            leaf_value = state.terminal_value()

        node.update_recursive(leaf_value)

    def getAction(self, realBoard):
        root_state = self.cloneBoard(realBoard)

        if root_state.status != STATUS_PLAYING:
            return -1

        root = TreeNode(parent=None, prior_p=1.0)

        for _ in range(self.simulations):
            self._run_simulation(root, root_state)

        if not root.children:
            valid = np.flatnonzero(root_state.get_valid_moves())
            return int(valid[0]) if valid.size > 0 else -1

        best_action, _ = max(
            root.children.items(),
            key=lambda item: item[1].n_visits,
        )
        return int(best_action)

    def cloneBoard(self, board):
        if isinstance(board, _BoardState):
            return board.clone()
        return _BoardState.from_cpp_board(board)
