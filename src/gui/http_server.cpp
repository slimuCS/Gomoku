#include "../../include/gomoku/gui/http_server.h"

#include <array>
#include <charconv>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using SocketHandle = SOCKET;
constexpr SocketHandle kBadSocket = INVALID_SOCKET;
static void closeSocket(SocketHandle s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kBadSocket = -1;
static void closeSocket(SocketHandle s) { close(s); }
#endif

namespace gomoku::gui {

static bool parseInt(const std::string& s, int& out) {
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && p == s.data() + s.size();
}

static std::string urlDecode(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.size()) {
            int v = 0;
            auto [p, ec] = std::from_chars(s.data() + i + 1, s.data() + i + 3, v, 16);
            if (ec == std::errc{}) { out += static_cast<char>(v); i += 2; }
            else { out += s[i]; }
        } else {
            out += s[i];
        }
    }
    return out;
}

static std::string formValue(const std::string& body, const std::string& key) {
    std::string search = key + "=";
    auto pos = body.find(search);
    if (pos == std::string::npos) return {};
    pos += search.size();
    auto end = body.find('&', pos);
    return urlDecode(end == std::string::npos ? body.substr(pos) : body.substr(pos, end - pos));
}

std::string HttpServer::stateJson() const {
    const Board& b = session_->board();
    const int sz = b.getSize();
    const auto& rules = session_->rules();

    std::ostringstream o;
    o << "{";
    o << "\"size\":" << sz << ",";
    o << "\"board\":[";
    for (int y = 0; y < sz; ++y) {
        for (int x = 0; x < sz; ++x) {
            if (x || y) o << ',';
            switch (b.getStone(x, y)) {
                case Stone::BLACK: o << '1'; break;
                case Stone::WHITE: o << '2'; break;
                default:          o << '0'; break;
            }
        }
    }
    o << "],";
    o << "\"current\":";
    o << (b.getCurrentPlayer() == Stone::BLACK ? "\"black\"" : "\"white\"") << ',';
    o << "\"status\":";
    switch (session_->status()) {
        case GameStatus::BLACK_WIN: o << "\"black_win\""; break;
        case GameStatus::WHITE_WIN: o << "\"white_win\""; break;
        case GameStatus::DRAW:      o << "\"draw\"";      break;
        default:                    o << "\"playing\"";   break;
    }
    o << ',';
    o << "\"mode\":";
    o << (session_->mode() == SessionMode::PVE ? "\"pve\"" : "\"pvp\"") << ',';

    auto lm = session_->last_move();
    if (lm) o << "\"last\":{\"x\":" << lm->first << ",\"y\":" << lm->second << "},";
    else     o << "\"last\":null,";

    o << "\"move_count\":" << session_->move_history().size() << ',';

    o << "\"rules\":{"
      << "\"undo_enabled\":" << (rules.undo_enabled ? "true" : "false") << ","
      << "\"timer_enabled\":" << (rules.timer_enabled ? "true" : "false") << ","
      << "\"timer_seconds\":" << rules.timer_seconds
      << "},";

    o << "\"pending_size\":" << pending_board_size_;
    o << "}";
    return o.str();
}

std::string HttpServer::response200(const std::string& ct, const std::string& body) {
    std::ostringstream o;
    o << "HTTP/1.1 200 OK\r\n"
      << "Content-Type: " << ct << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Access-Control-Allow-Origin: *\r\n"
      << "Connection: close\r\n\r\n"
      << body;
    return o.str();
}

std::string HttpServer::response400(const std::string& msg) {
    std::string body = "{\"error\":\"" + msg + "\"}";
    std::ostringstream o;
    o << "HTTP/1.1 400 Bad Request\r\n"
      << "Content-Type: application/json\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Access-Control-Allow-Origin: *\r\n"
      << "Connection: close\r\n\r\n"
      << body;
    return o.str();
}

std::string HttpServer::handleRequest(const std::string& method,
                                      const std::string& path,
                                      const std::string& body) {
    if (path == "/" || path == "/index.html")
        return response200("text/html; charset=utf-8", htmlPage());

    if (path == "/api/state")
        return response200("application/json", stateJson());

    if (path == "/api/move" && method == "POST") {
        if (session_->status() != GameStatus::PLAYING)
            return response400("game_over");
        int x = 0, y = 0;
        if (!parseInt(formValue(body, "x"), x) || !parseInt(formValue(body, "y"), y))
            return response400("bad_coords");
        if (!session_->human_move(x, y, false))
            return response400("invalid_move");
        if (session_->mode() == SessionMode::PVE && session_->status() == GameStatus::PLAYING)
            session_->ai_move();
        return response200("application/json", stateJson());
    }

    if (path == "/api/start" && method == "POST") {
        auto mode_str = formValue(body, "mode");
        auto size_str = formValue(body, "size");
        int sz = pending_board_size_;
        if (!size_str.empty()) parseInt(size_str, sz);
        if (sz < 9) sz = 9;
        if (sz > 19) sz = 19;
        pending_board_size_ = sz;

        SessionMode mode = (mode_str == "pve") ? SessionMode::PVE : SessionMode::PVP;
        SessionRules prev_rules = session_->rules();

        session_ = std::make_unique<GameSession>(sz);
        session_->setRules(prev_rules);
        session_->start(mode);
        return response200("application/json", stateJson());
    }

    if (path == "/api/settings" && method == "POST") {
        auto undo_str  = formValue(body, "undo_enabled");
        auto timer_str = formValue(body, "timer_enabled");
        auto secs_str  = formValue(body, "timer_seconds");
        auto size_str  = formValue(body, "pending_size");

        SessionRules rules = session_->rules();
        if (!undo_str.empty())  rules.undo_enabled  = (undo_str  == "1" || undo_str  == "true");
        if (!timer_str.empty()) rules.timer_enabled = (timer_str == "1" || timer_str == "true");
        if (!secs_str.empty()) {
            int s = 0;
            if (parseInt(secs_str, s) && s >= 1 && s <= 999) rules.timer_seconds = s;
        }
        if (!size_str.empty()) {
            int sz = 0;
            if (parseInt(size_str, sz) && sz >= 9 && sz <= 19) pending_board_size_ = sz;
        }
        session_->setRules(rules);
        return response200("application/json", stateJson());
    }

    if (path == "/api/undo" && method == "POST") {
        if (!session_->rules().undo_enabled)
            return response400("undo_disabled");
        session_->undo();
        if (session_->mode() == SessionMode::PVE) session_->undo();
        return response200("application/json", stateJson());
    }

    if (path == "/api/timeout" && method == "POST") {
        if (session_->status() == GameStatus::PLAYING)
            session_->skipTurn();
        if (session_->mode() == SessionMode::PVE && session_->status() == GameStatus::PLAYING)
            session_->ai_move();
        return response200("application/json", stateJson());
    }

    if (method == "OPTIONS")
        return "HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET,POST\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

    return "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
}

HttpServer::HttpServer() : session_(std::make_unique<GameSession>(15)) {
    session_->start(SessionMode::PVP);
}

void HttpServer::run(std::uint16_t port) {
#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    SocketHandle server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == kBadSocket) { std::cerr << "Failed to create socket\n"; return; }

    int opt = 1;
#ifdef _WIN32
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "bind() failed on port " << port << '\n';
        closeSocket(server);
        return;
    }
    listen(server, 8);
    std::cout << "Gomoku GUI running at http://localhost:" << port << '\n';

#ifdef _WIN32
    std::system(("start http://localhost:" + std::to_string(port)).c_str());
#elif __APPLE__
    std::system(("open http://localhost:" + std::to_string(port)).c_str());
#else
    std::system(("xdg-open http://localhost:" + std::to_string(port)).c_str());
#endif

    while (true) {
        SocketHandle client = accept(server, nullptr, nullptr);
        if (client == kBadSocket) continue;

        std::string raw;
        std::array<char, 4096> buf{};
        while (true) {
#ifdef _WIN32
            int n = recv(client, buf.data(), static_cast<int>(buf.size()), 0);
#else
            auto n = recv(client, buf.data(), buf.size(), 0);
#endif
            if (n <= 0) break;
            raw.append(buf.data(), static_cast<std::size_t>(n));
            auto header_end = raw.find("\r\n\r\n");
            if (header_end == std::string::npos) continue;
            std::size_t cl = 0;
            auto cl_pos = raw.find("Content-Length: ");
            if (cl_pos != std::string::npos) {
                cl_pos += 16;
                auto cl_end = raw.find("\r\n", cl_pos);
                std::string cl_str = raw.substr(cl_pos, cl_end - cl_pos);
                int clv = 0;
                if (parseInt(cl_str, clv)) cl = static_cast<std::size_t>(clv);
            }
            if (raw.size() - (header_end + 4) >= cl) break;
        }

        if (raw.empty()) { closeSocket(client); continue; }

        auto line_end = raw.find("\r\n");
        std::istringstream rls(raw.substr(0, line_end));
        std::string method, path, proto;
        rls >> method >> path >> proto;

        auto qpos = path.find('?');
        if (qpos != std::string::npos) path = path.substr(0, qpos);

        std::string req_body;
        auto body_start = raw.find("\r\n\r\n");
        if (body_start != std::string::npos) req_body = raw.substr(body_start + 4);

        std::string resp = handleRequest(method, path, req_body);
#ifdef _WIN32
        send(client, resp.data(), static_cast<int>(resp.size()), 0);
#else
        send(client, resp.data(), resp.size(), 0);
#endif
        closeSocket(client);
    }
    closeSocket(server);
}

std::string HttpServer::htmlPage() {
    return R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Gomoku</title>
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
  background: #1a1a2e;
  display: flex;
  justify-content: center;
  align-items: flex-start;
  min-height: 100vh;
  padding: 24px;
  font-family: 'Segoe UI', sans-serif;
  color: #eee;
  gap: 24px;
}
canvas {
  border-radius: 8px;
  box-shadow: 0 8px 32px rgba(0,0,0,0.6);
  cursor: crosshair;
  flex-shrink: 0;
}
#panel {
  width: 210px;
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding-top: 4px;
}
h1 { font-size: 1.4rem; letter-spacing: 3px; color: #f0c040; }
.card {
  background: #16213e;
  border-radius: 8px;
  padding: 12px 14px;
}
.card-title {
  font-size: 0.7rem;
  text-transform: uppercase;
  letter-spacing: 1.5px;
  color: #7a8aa0;
  margin-bottom: 8px;
}
#turn-indicator { display: flex; align-items: center; gap: 8px; }
#turn-stone { width: 22px; height: 22px; border-radius: 50%; border: 2px solid #444; flex-shrink: 0; }
#turn-name { font-weight: 600; font-size: 1rem; }
#mode-text { font-size: 0.85rem; color: #aaa; }
#timer-bar { width: 100%; height: 4px; background: #0d1b2e; border-radius: 2px; margin-top: 8px; overflow: hidden; }
#timer-fill { height: 100%; width: 100%; background: #3a86ff; transition: width 0.9s linear, background 0.3s; border-radius: 2px; }
#timer-text { font-size: 0.8rem; color: #aaa; margin-top: 4px; }
#result-banner {
  display: none; background: #f0c040; color: #1a1a2e;
  border-radius: 8px; padding: 10px; font-weight: 700;
  text-align: center; font-size: 1rem;
}
.btn {
  width: 100%; padding: 9px; border: none; border-radius: 6px;
  font-size: 0.82rem; font-weight: 600; cursor: pointer;
  transition: filter 0.15s; letter-spacing: 0.3px;
}
.btn:hover { filter: brightness(1.15); }
.btn-pvp  { background: #e07b39; color: #fff; }
.btn-pve  { background: #3a86ff; color: #fff; }
.btn-undo { background: #2d2d44; color: #ccc; border: 1px solid #444; }
.row { display: flex; gap: 8px; }
.row .btn { flex: 1; }
.size-btns { display: flex; gap: 6px; flex-wrap: wrap; }
.size-btn {
  flex: 1; padding: 6px 4px; border: 1px solid #334; border-radius: 5px;
  background: #0d1b2e; color: #aaa; font-size: 0.78rem; cursor: pointer;
  transition: all 0.15s;
}
.size-btn.active { background: #3a86ff; color: #fff; border-color: #3a86ff; }
.size-btn:hover { border-color: #3a86ff; color: #eee; }
.toggle-row {
  display: flex; justify-content: space-between; align-items: center;
  padding: 2px 0;
}
.toggle-label { font-size: 0.85rem; color: #ccc; }
.toggle {
  position: relative; width: 38px; height: 20px; cursor: pointer;
  background: #334; border-radius: 10px; transition: background 0.2s;
}
.toggle.on { background: #3a86ff; }
.toggle::after {
  content: ''; position: absolute; width: 14px; height: 14px;
  background: #fff; border-radius: 50%; top: 3px; left: 3px; transition: left 0.2s;
}
.toggle.on::after { left: 21px; }
.secs-row { display: flex; align-items: center; gap: 6px; margin-top: 8px; }
.secs-row label { font-size: 0.8rem; color: #888; flex-shrink: 0; }
.secs-input {
  width: 52px; padding: 4px 6px; border-radius: 4px; border: 1px solid #334;
  background: #0d1b2e; color: #eee; font-size: 0.85rem; text-align: center;
}
.secs-input:focus { outline: none; border-color: #3a86ff; }
</style>
</head>
<body>
<canvas id="board" width="600" height="600"></canvas>
<div id="panel">
  <h1>GOMOKU</h1>

  <div class="card" id="status-card">
    <div class="card-title">Status</div>
    <div id="turn-indicator">
      <div id="turn-stone"></div>
      <span id="turn-name">Black</span>
    </div>
    <div id="mode-text" style="margin-top:6px">PvP</div>
    <div id="timer-bar"><div id="timer-fill"></div></div>
    <div id="timer-text"></div>
  </div>

  <div id="result-banner"></div>

  <div class="card">
    <div class="card-title">New Game</div>
    <div class="row">
      <button class="btn btn-pvp"  onclick="startGame('pvp')">PvP</button>
      <button class="btn btn-pve"  onclick="startGame('pve')">PvE</button>
    </div>
  </div>

  <button class="btn btn-undo" onclick="undo()">Undo</button>

  <div class="card">
    <div class="card-title">Board Size</div>
    <div class="size-btns">
      <button class="size-btn" data-sz="9"  onclick="setSize(9)">9x9</button>
      <button class="size-btn" data-sz="13" onclick="setSize(13)">13x13</button>
      <button class="size-btn active" data-sz="15" onclick="setSize(15)">15x15</button>
      <button class="size-btn" data-sz="19" onclick="setSize(19)">19x19</button>
    </div>
    <div style="font-size:0.72rem;color:#666;margin-top:6px">Applied on next New Game</div>
  </div>

  <div class="card">
    <div class="card-title">Rules</div>
    <div class="toggle-row">
      <span class="toggle-label">Undo</span>
      <div class="toggle on" id="toggle-undo" onclick="toggleSetting('undo')"></div>
    </div>
    <div class="toggle-row" style="margin-top:8px">
      <span class="toggle-label">Timer</span>
      <div class="toggle" id="toggle-timer" onclick="toggleSetting('timer')"></div>
    </div>
    <div class="secs-row" id="secs-row" style="opacity:0.4;pointer-events:none">
      <label>Seconds:</label>
      <input class="secs-input" id="secs-input" type="number" min="1" max="999" value="20"
             onchange="saveTimerSecs()" oninput="saveTimerSecs()">
    </div>
  </div>
</div>

<script>
const CANVAS_SIZE = 600;
const PADDING = 30;
const canvas = document.getElementById('board');
const ctx = canvas.getContext('2d');

let state = null;
let timerInterval = null;
let timerRemaining = 0;
let lastMoveCount = -1;
let pendingSize = 15;

function cellSize(sz) { return (CANVAS_SIZE - PADDING * 2) / (sz - 1); }

function drawBoard(s) {
  const sz = s.size;
  const cell = cellSize(sz);
  const grad = ctx.createLinearGradient(0, 0, CANVAS_SIZE, CANVAS_SIZE);
  grad.addColorStop(0, '#d4a04a');
  grad.addColorStop(1, '#c4902a');
  ctx.fillStyle = grad;
  ctx.fillRect(0, 0, CANVAS_SIZE, CANVAS_SIZE);

  ctx.strokeStyle = '#8B6914';
  ctx.lineWidth = 1;
  for (let i = 0; i < sz; i++) {
    const x = PADDING + i * cell;
    const y = PADDING + i * cell;
    ctx.beginPath(); ctx.moveTo(x, PADDING); ctx.lineTo(x, CANVAS_SIZE - PADDING); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(PADDING, y); ctx.lineTo(CANVAS_SIZE - PADDING, y); ctx.stroke();
  }

  if (sz === 15) {
    const stars = [[3,3],[3,11],[11,3],[11,11],[7,7],[3,7],[7,3],[11,7],[7,11]];
    ctx.fillStyle = '#8B6914';
    for (const [sx, sy] of stars) {
      ctx.beginPath();
      ctx.arc(PADDING + sx * cell, PADDING + sy * cell, 3.5, 0, Math.PI * 2);
      ctx.fill();
    }
  }
  if (sz === 19) {
    const stars = [[3,3],[3,9],[3,15],[9,3],[9,9],[9,15],[15,3],[15,9],[15,15]];
    ctx.fillStyle = '#8B6914';
    for (const [sx, sy] of stars) {
      ctx.beginPath();
      ctx.arc(PADDING + sx * cell, PADDING + sy * cell, 3.5, 0, Math.PI * 2);
      ctx.fill();
    }
  }

  for (let y = 0; y < sz; y++) {
    for (let x = 0; x < sz; x++) {
      const v = s.board[y * sz + x];
      if (v === 0) continue;
      const cx = PADDING + x * cell;
      const cy = PADDING + y * cell;
      const r = cell * 0.46;
      if (v === 1) {
        const g = ctx.createRadialGradient(cx-r*0.3, cy-r*0.3, r*0.05, cx, cy, r);
        g.addColorStop(0, '#555'); g.addColorStop(1, '#0a0a0a');
        ctx.fillStyle = g;
      } else {
        const g = ctx.createRadialGradient(cx-r*0.3, cy-r*0.3, r*0.05, cx, cy, r);
        g.addColorStop(0, '#fff'); g.addColorStop(1, '#ccc');
        ctx.fillStyle = g;
      }
      ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2);
      ctx.shadowColor = 'rgba(0,0,0,0.45)'; ctx.shadowBlur = 6;
      ctx.fill(); ctx.shadowBlur = 0;
    }
  }

  if (s.last) {
    const mx = PADDING + s.last.x * cell;
    const my = PADDING + s.last.y * cell;
    ctx.fillStyle = 'rgba(255,60,60,0.85)';
    ctx.beginPath(); ctx.arc(mx, my, cell * 0.13, 0, Math.PI * 2); ctx.fill();
  }
}

function startClientTimer(totalSecs) {
  clearInterval(timerInterval);
  timerRemaining = totalSecs;
  const fill = document.getElementById('timer-fill');
  const txt  = document.getElementById('timer-text');
  function tick() {
    if (timerRemaining <= 0) {
      clearInterval(timerInterval);
      txt.textContent = 'Time up!';
      fill.style.width = '0%';
      fill.style.background = '#e63946';
      fetch('/api/timeout', { method: 'POST' }).then(fetchState);
      return;
    }
    const pct = (timerRemaining / totalSecs) * 100;
    fill.style.width = pct + '%';
    fill.style.background = timerRemaining <= 5 ? '#e63946' : '#3a86ff';
    txt.textContent = timerRemaining + 's remaining';
    timerRemaining--;
  }
  tick();
  timerInterval = setInterval(tick, 1000);
}

function stopClientTimer() {
  clearInterval(timerInterval);
  document.getElementById('timer-fill').style.width = '100%';
  document.getElementById('timer-text').textContent = '';
}

function updatePanel(s) {
  const turnStone = document.getElementById('turn-stone');
  const turnName  = document.getElementById('turn-name');
  const modeText  = document.getElementById('mode-text');
  const banner    = document.getElementById('result-banner');

  modeText.textContent = s.mode === 'pve' ? 'PvE  (you = Black)' : 'PvP';

  if (s.status === 'playing') {
    banner.style.display = 'none';
    turnStone.style.background = s.current === 'black' ? '#111' : '#eee';
    turnName.textContent = s.current === 'black' ? 'Black' : 'White';
    if (s.rules.timer_enabled) {
      if (s.move_count !== lastMoveCount) {
        lastMoveCount = s.move_count;
        startClientTimer(s.rules.timer_seconds);
      }
    } else {
      stopClientTimer();
    }
  } else {
    stopClientTimer();
    const msgs = { black_win: 'Black Wins!', white_win: 'White Wins!', draw: 'Draw!' };
    banner.textContent = msgs[s.status] || s.status;
    banner.style.display = 'block';
    turnName.textContent = '--';
    turnStone.style.background = 'transparent';
  }

  document.getElementById('toggle-undo').className  = 'toggle ' + (s.rules.undo_enabled  ? 'on' : '');
  document.getElementById('toggle-timer').className = 'toggle ' + (s.rules.timer_enabled ? 'on' : '');
  const secsRow = document.getElementById('secs-row');
  secsRow.style.opacity = s.rules.timer_enabled ? '1' : '0.4';
  secsRow.style.pointerEvents = s.rules.timer_enabled ? 'auto' : 'none';
  document.getElementById('secs-input').value = s.rules.timer_seconds;

  document.querySelectorAll('.size-btn').forEach(b => {
    b.classList.toggle('active', parseInt(b.dataset.sz) === s.pending_size);
  });
}

async function fetchState() {
  try {
    const r = await fetch('/api/state');
    state = await r.json();
    drawBoard(state);
    updatePanel(state);
  } catch(e) {
    console.error('fetchState:', e);
  }
}

async function startGame(mode) {
  await fetch('/api/start', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: 'mode=' + mode + '&size=' + pendingSize
  });
  lastMoveCount = -1;
  clearInterval(timerInterval);
  fetchState();
}

async function undo() {
  await fetch('/api/undo', { method: 'POST' });
  fetchState();
}

async function setSize(sz) {
  pendingSize = sz;
  await fetch('/api/settings', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: 'pending_size=' + sz
  });
  fetchState();
}

async function toggleSetting(which) {
  const key = which === 'undo' ? 'undo_enabled' : 'timer_enabled';
  const cur = which === 'undo'
    ? (state && state.rules.undo_enabled)
    : (state && state.rules.timer_enabled);
  await fetch('/api/settings', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: key + '=' + (cur ? '0' : '1')
  });
  fetchState();
}

async function saveTimerSecs() {
  const v = document.getElementById('secs-input').value;
  await fetch('/api/settings', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: 'timer_seconds=' + v
  });
}

canvas.addEventListener('click', async (e) => {
  if (!state || state.status !== 'playing') return;
  if (state.mode === 'pve' && state.current !== 'black') return;
  const rect = canvas.getBoundingClientRect();
  const sx = CANVAS_SIZE / rect.width;
  const sy = CANVAS_SIZE / rect.height;
  const px = (e.clientX - rect.left) * sx;
  const py = (e.clientY - rect.top)  * sy;
  const cell = cellSize(state.size);
  const x = Math.round((px - PADDING) / cell);
  const y = Math.round((py - PADDING) / cell);
  if (x < 0 || y < 0 || x >= state.size || y >= state.size) return;
  await fetch('/api/move', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: 'x=' + x + '&y=' + y
  });
  fetchState();
});

setInterval(fetchState, 500);
fetchState();
</script>
</body>
</html>
)html";
}

} // namespace gomoku::gui
