# _*_ coding : utf-8_*_
# @Time : 2026/3/24 下午 09:21
# @Author : 黃仕璿
# @File : model.py
# @Project : Gomoku
import numpy as np
import gomoku_ai
board = gomoku_ai.Board(15)

board.place_stone(7, 7)

obs = board.get_observation()

print(obs)  # Should be (3, 15, 15)

assert obs[1][7][7] == 1.0
assert np.sum(obs[1]) == 1.0
assert obs[2, 0,0] == 1.0