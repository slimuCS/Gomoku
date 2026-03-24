# _*_ coding : utf-8_*_
# @Time : 2026/3/24 下午 09:21
# @Author : 黃仕璿
# @File : model.py
# @Project : Gomoku
import numpy as np
import torch
import gomoku_ai

class TreeNode:
    def __init__(self, parent, prior_p):
        self.parent = parent
        self.children = {}
        self.n_visits = 0
        self.Q = 0
        self.P = prior_p
    def isleaf(self):
        return len(self.children) == 0
    def isroot(self):
        return self.parent is None
class MCTS:
    def __init__(self, model, simulations=1000):
        self.model = model
        self.simulations = simulations
    def getAction(self, realBoard):
        root = TreeNode(parent=None, prior_p= 1.0)

        for _ in range(self.simulations):
            boardPtr = self.cloneBoard(realBoard)
            node = root

            while node.isleaf():
                action, node = self.select(cpuct = 5)
                r, c = divmod(action, 15)
                boardPtr.place(r, c)

            obs = boardPtr.get_observation()
            policy, value = self.model(torch.from_numpy(obs).float().unsqueeze(0))

            policy = policy.detach().numpy()[0]
            value = value.item()
            valid_moves = boardPtr.get_valid_moves()
            policy = policy * valid_moves
    def cloneBoard(self, board):
        new_board = gomoku_ai.Board(board.width, board.height)
        new_board.board = np.copy(board.board)
        return new_board

    def select(self, cpuct):
        best_action = -1
        best_node = None
        max_u = -float('inf')

        for action, node in self.children.items():
            u = node.Q + cpuct * node.P * np.sqrt(node.parent.n_visits) / (1 + node.n_visits)
            if u > max_u:
                max_u = u
                best_action = action
                best_node = node

        return best_action, best_node