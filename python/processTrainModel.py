import argparse
import random
from pathlib import Path
import sys

import numpy as np
import torch

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from gomoku_net import GomokuNet, count_parameters, quantize_dynamic_linear, state_dict_size_mb
from training_extensions import ArenaEvaluator, CheckpointManager, TacticsChecker, TrainingMetrics

DEFAULT_BOARD_SIZE = 15
DEFAULT_HIDDEN_DIM = 512
DEFAULT_TRUNK_LAYERS = 12
DEFAULT_BEST_WINRATE_THRESHOLD = 0.55


def _seed_everything(seed):
    if seed is None:
        return

    seed = int(seed)
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


def _fmt_float(value, digits=4):
    if value is None:
        return "NA"
    return f"{float(value):.{digits}f}"


def train(
    board_size=DEFAULT_BOARD_SIZE,
    hidden_dim=DEFAULT_HIDDEN_DIM,
    trunk_layers=DEFAULT_TRUNK_LAYERS,
    iterations=30,
    replay_capacity=50000,
    num_self_play_games=32,
    simulations=320,
    cpuct=3,
    batch_size=128,
    updates_per_iteration=48,
    min_buffer_size=1024,
    temperature=1.0,
    temperature_drop_move=8,
    candidate_radius=3,
    max_candidates=72,
    forced_check_depth=1,
    dirichlet_alpha=0.3,
    dirichlet_epsilon=0.25,
    augment_symmetries=True,
    max_grad_norm=1.5,
    lr=1e-3,
    weight_decay=1e-4,
    min_lr=1e-5,
    use_amp=True,
    policy_label_smoothing=0.03,
    value_loss_type="huber",
    seed=None,
    training_log_path="training_log.jsonl",
    eval_every=2,
    arena_games_vs_random=12,
    arena_games_vs_best=20,
    arena_simulations=96,
    arena_cpuct=None,
    best_model_path="gomoku_model_best.pt",
    last_model_path="gomoku_model_last.pt",
    periodic_checkpoint_dir="checkpoints",
    periodic_checkpoint_every=5,
    best_winrate_threshold=DEFAULT_BEST_WINRATE_THRESHOLD,
    tactics_use_mcts=False,
    tactics_mcts_simulations=64,
):
    from model import ReplayBuffer, run_training_iteration

    _seed_everything(seed)
    if hasattr(torch, "set_float32_matmul_precision"):
        torch.set_float32_matmul_precision("high")

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    use_amp = bool(use_amp and device.type == "cuda")

    net = GomokuNet(
        board_size=board_size,
        hidden_dim=hidden_dim,
        trunk_layers=trunk_layers,
    ).to(device)
    optimizer = torch.optim.AdamW(net.parameters(), lr=lr, weight_decay=weight_decay)

    total_updates = max(1, int(iterations) * max(1, int(updates_per_iteration)))
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(
        optimizer,
        T_max=total_updates,
        eta_min=max(0.0, min(float(min_lr), float(lr))),
    )

    scaler = None
    if use_amp:
        try:
            scaler = torch.amp.GradScaler("cuda", enabled=True)
        except (AttributeError, TypeError):
            scaler = torch.cuda.amp.GradScaler(enabled=True)

    replay = ReplayBuffer(capacity=replay_capacity)
    metrics_logger = TrainingMetrics(log_path=training_log_path)
    checkpoint_manager = CheckpointManager(
        best_path=best_model_path,
        last_path=last_model_path,
        periodic_dir=periodic_checkpoint_dir,
        periodic_every=periodic_checkpoint_every,
    )
    checkpoint_manager.ensure_best_exists(net, iteration=0)

    arena_evaluator = ArenaEvaluator(
        board_size=board_size,
        simulations=arena_simulations,
        cpuct=float(arena_cpuct) if arena_cpuct is not None else float(cpuct),
        candidate_radius=candidate_radius,
        max_candidates=max_candidates,
        forced_check_depth=forced_check_depth,
        seed=seed,
    )

    tactics_checker = TacticsChecker(
        board_size=board_size,
        use_mcts=tactics_use_mcts,
        mcts_simulations=tactics_mcts_simulations,
        cpuct=cpuct,
        candidate_radius=candidate_radius,
        max_candidates=max_candidates,
        forced_check_depth=forced_check_depth,
    )

    print(
        f"device={device}, amp={use_amp}, params={count_parameters(net):,}, "
        f"hidden_dim={hidden_dim}, trunk_layers={trunk_layers}, "
        f"state_dict_size={state_dict_size_mb(net):.2f} MB, "
        f"cpuct={cpuct}, sims={simulations}, augment={augment_symmetries}, "
        f"loss={value_loss_type}, label_smoothing={policy_label_smoothing}, seed={seed}"
    )
    print(
        f"metrics_log={training_log_path}, eval_every={eval_every}, "
        f"arena_games(random/best)={arena_games_vs_random}/{arena_games_vs_best}, "
        f"best_threshold={best_winrate_threshold:.2f}, "
        f"tactics_puzzles={len(tactics_checker.puzzles)} ({'mcts' if tactics_use_mcts else 'policy'})"
    )

    for i in range(iterations):
        iter_idx = i + 1
        summary = run_training_iteration(
            model=net,
            optimizer=optimizer,
            replay_buffer=replay,
            num_self_play_games=num_self_play_games,
            simulations=simulations,
            cpuct=cpuct,
            board_size=board_size,
            batch_size=batch_size,
            updates_per_iteration=updates_per_iteration,
            min_buffer_size=min_buffer_size,
            temperature=temperature,
            temperature_drop_move=temperature_drop_move,
            candidate_radius=candidate_radius,
            max_candidates=max_candidates,
            forced_check_depth=forced_check_depth,
            dirichlet_alpha=dirichlet_alpha,
            dirichlet_epsilon=dirichlet_epsilon,
            augment_symmetries=augment_symmetries,
            max_grad_norm=max_grad_norm,
            device=device,
            scheduler=scheduler,
            scaler=scaler,
            use_amp=use_amp,
            policy_label_smoothing=policy_label_smoothing,
            value_loss_type=value_loss_type,
        )

        checkpoint_manager.save_last(net, iteration=iter_idx)
        periodic_path = checkpoint_manager.save_periodic(net, iteration=iter_idx)

        tactics_result = tactics_checker.evaluate(net, device=device)

        arena_result = {
            "evaluated": False,
            "vs_random": None,
            "vs_previous_best": None,
            "best_promoted": False,
            "best_threshold": float(best_winrate_threshold),
        }

        if int(eval_every) > 0 and iter_idx % int(eval_every) == 0:
            arena_result["evaluated"] = True
            arena_result["vs_random"] = arena_evaluator.evaluate_vs_random(
                model=net,
                games=arena_games_vs_random,
            )

            previous_best = checkpoint_manager.load_best_model_like(net, device=device)
            if previous_best is not None:
                vs_best = arena_evaluator.evaluate_vs_model(
                    model=net,
                    opponent_model=previous_best,
                    games=arena_games_vs_best,
                )
                arena_result["vs_previous_best"] = vs_best

                if float(vs_best["win_rate"]) > float(best_winrate_threshold):
                    checkpoint_manager.save_best(
                        net,
                        iteration=iter_idx,
                        metadata={
                            "promotion_reason": "win_rate_above_threshold",
                            "promotion_threshold": float(best_winrate_threshold),
                            "vs_previous_best": vs_best,
                        },
                    )
                    arena_result["best_promoted"] = True
            else:
                checkpoint_manager.save_best(
                    net,
                    iteration=iter_idx,
                    metadata={"promotion_reason": "no_previous_best"},
                )
                arena_result["best_promoted"] = True

        log_entry = {
            "iteration": int(iter_idx),
            "training": {
                "policy_loss": summary.get("policy_loss"),
                "value_loss": summary.get("value_loss"),
                "policy_entropy": summary.get("policy_entropy"),
                "value_mean": summary.get("value_mean"),
                "updates_run": int(summary.get("updates_run", 0)),
                "buffer_size": int(summary.get("buffer_size", 0)),
            },
            "arena": arena_result,
            "tactics": tactics_result,
        }
        if periodic_path is not None:
            log_entry["periodic_checkpoint"] = str(periodic_path)
        metrics_logger.append(log_entry)

        curve_text = (
            f"pl={_fmt_float(summary.get('policy_loss'))}, "
            f"vl={_fmt_float(summary.get('value_loss'))}, "
            f"pe={_fmt_float(summary.get('policy_entropy'))}, "
            f"vm={_fmt_float(summary.get('value_mean'))}"
        )
        tactics_text = (
            f"tactics={tactics_result['hits']}/{tactics_result['total']} "
            f"({tactics_result['hit_rate'] * 100:.1f}%)"
        )
        if arena_result["evaluated"]:
            random_wr = arena_result["vs_random"]["win_rate"] * 100.0 if arena_result["vs_random"] else 0.0
            if arena_result["vs_previous_best"] is None:
                best_wr_text = "N/A"
            else:
                best_wr_text = f"{arena_result['vs_previous_best']['win_rate'] * 100.0:.1f}%"
            arena_text = (
                f"arena=random:{random_wr:.1f}% "
                f"prev_best:{best_wr_text} "
                f"promoted={arena_result['best_promoted']}"
            )
        else:
            arena_text = "arena=skip"

        print(f"[iter {iter_idx:03d}/{iterations}] {curve_text} | {tactics_text} | {arena_text}")

    return net, device


def main():
    parser = argparse.ArgumentParser(description="Train Gomoku model and optionally compress it.")
    parser.add_argument("--board-size", type=int, default=DEFAULT_BOARD_SIZE)
    parser.add_argument("--hidden-dim", type=int, default=DEFAULT_HIDDEN_DIM, help="Lower this to shrink model size.")
    parser.add_argument(
        "--trunk-layers",
        type=int,
        default=DEFAULT_TRUNK_LAYERS,
        help="Use 1 for the legacy single-hidden-layer model.",
    )
    parser.add_argument("--iterations", type=int, default=30)
    parser.add_argument("--self-play-games", type=int, default=12)
    parser.add_argument("--simulations", type=int, default=320)
    parser.add_argument("--cpuct", type=float, default=4.2)
    parser.add_argument("--batch-size", type=int, default=128)
    parser.add_argument("--updates-per-iteration", type=int, default=16)
    parser.add_argument("--min-buffer-size", type=int, default=1024)
    parser.add_argument("--temperature", type=float, default=1.0)
    parser.add_argument("--temperature-drop-move", type=int, default=12)
    parser.add_argument("--candidate-radius", type=int, default=2)
    parser.add_argument("--max-candidates", type=int, default=72)
    parser.add_argument("--forced-check-depth", type=int, default=1)
    parser.add_argument("--dirichlet-alpha", type=float, default=0.3)
    parser.add_argument("--dirichlet-epsilon", type=float, default=0.25)
    parser.add_argument(
        "--no-augment-symmetries",
        action="store_true",
        help="Disable 8-way board symmetry augmentation in replay buffer.",
    )
    parser.add_argument("--max-grad-norm", type=float, default=1.5)
    parser.add_argument("--replay-capacity", type=int, default=50000)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--min-lr", type=float, default=1e-5)
    parser.add_argument("--weight-decay", type=float, default=1e-4)
    parser.add_argument("--seed", type=int, default=None)
    parser.add_argument("--no-amp", action="store_true")
    parser.add_argument("--policy-label-smoothing", type=float, default=0.03)
    parser.add_argument("--value-loss-type", choices=("mse", "huber"), default="huber")

    parser.add_argument("--training-log-path", default="training_log.jsonl")
    parser.add_argument("--eval-every", type=int, default=2, help="Arena evaluation interval in iterations.")
    parser.add_argument("--arena-games-vs-random", type=int, default=12)
    parser.add_argument("--arena-games-vs-best", type=int, default=20)
    parser.add_argument("--arena-simulations", type=int, default=96)
    parser.add_argument("--arena-cpuct", type=float, default=None)
    parser.add_argument("--best-model-path", default="gomoku_model_best.pt")
    parser.add_argument("--last-model-path", default="gomoku_model_last.pt")
    parser.add_argument("--periodic-checkpoint-dir", default="checkpoints")
    parser.add_argument("--periodic-checkpoint-every", type=int, default=5)
    parser.add_argument("--best-winrate-threshold", type=float, default=DEFAULT_BEST_WINRATE_THRESHOLD)
    parser.add_argument("--tactics-use-mcts", action="store_true")
    parser.add_argument("--tactics-mcts-simulations", type=int, default=64)

    parser.add_argument("--save-path", default="gomoku_model.pt")
    parser.add_argument("--quantize", action="store_true", help="Export int8 dynamic-quantized model.")
    parser.add_argument("--quantized-save-path", default="gomoku_model_int8.pt")

    args = parser.parse_args()

    net, device = train(
        board_size=args.board_size,
        hidden_dim=args.hidden_dim,
        trunk_layers=args.trunk_layers,
        iterations=args.iterations,
        replay_capacity=args.replay_capacity,
        num_self_play_games=args.self_play_games,
        simulations=args.simulations,
        cpuct=args.cpuct,
        batch_size=args.batch_size,
        updates_per_iteration=args.updates_per_iteration,
        min_buffer_size=args.min_buffer_size,
        temperature=args.temperature,
        temperature_drop_move=args.temperature_drop_move,
        candidate_radius=args.candidate_radius,
        max_candidates=args.max_candidates,
        forced_check_depth=args.forced_check_depth,
        dirichlet_alpha=args.dirichlet_alpha,
        dirichlet_epsilon=args.dirichlet_epsilon,
        augment_symmetries=not args.no_augment_symmetries,
        max_grad_norm=args.max_grad_norm,
        lr=args.lr,
        min_lr=args.min_lr,
        weight_decay=args.weight_decay,
        use_amp=not args.no_amp,
        policy_label_smoothing=args.policy_label_smoothing,
        value_loss_type=args.value_loss_type,
        seed=args.seed,
        training_log_path=args.training_log_path,
        eval_every=args.eval_every,
        arena_games_vs_random=args.arena_games_vs_random,
        arena_games_vs_best=args.arena_games_vs_best,
        arena_simulations=args.arena_simulations,
        arena_cpuct=args.arena_cpuct,
        best_model_path=args.best_model_path,
        last_model_path=args.last_model_path,
        periodic_checkpoint_dir=args.periodic_checkpoint_dir,
        periodic_checkpoint_every=args.periodic_checkpoint_every,
        best_winrate_threshold=args.best_winrate_threshold,
        tactics_use_mcts=args.tactics_use_mcts,
        tactics_mcts_simulations=args.tactics_mcts_simulations,
    )

    net_cpu = net.to("cpu").eval()
    torch.save(net_cpu.state_dict(), args.save_path)
    fp32_size = state_dict_size_mb(net_cpu)
    print(f"saved fp32 model -> {args.save_path} ({fp32_size:.2f} MB)")

    if args.quantize:
        qnet = quantize_dynamic_linear(net_cpu)
        torch.save(qnet.state_dict(), args.quantized_save_path)
        int8_size = state_dict_size_mb(qnet)
        ratio = (int8_size / fp32_size) if fp32_size > 1e-12 else 1.0
        print(
            f"saved int8 model -> {args.quantized_save_path} "
            f"({int8_size:.2f} MB, {ratio:.2%} of fp32 size)"
        )

    # Move back to the original device only if more work follows in-process.
    net.to(device)


if __name__ == "__main__":
    main()
