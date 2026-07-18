"""Maritime intelligence agent — CLI entry point.

Usage:
  # Single query
  python main.py "Is MMSI 338234631 behaving suspiciously?"

  # Interactive mode
  python main.py

  # Point at a non-default engine
  python main.py --engine-url http://localhost:9090 "Scan for dark vessels"
"""
from __future__ import annotations

import argparse
import sys

from rich.console import Console
from rich.markdown import Markdown
from rich.panel import Panel
from rich.rule import Rule
from rich.spinner import Spinner
from rich.live import Live

from engine_client import EngineClient
from agent import MaritimeAgent

console = Console()


def run_query(agent: MaritimeAgent, query: str) -> None:
    console.print()
    console.print(Rule(f"[bold cyan]Investigating[/bold cyan]: {query}"))
    console.print()

    tool_calls: list[str] = []

    def on_text(text: str) -> None:
        # Printed at the end so we don't interleave with tool output
        pass

    def on_tool_call(name: str, inp: dict) -> None:
        mmsi = inp.get("mmsi", "")
        detail = f"  [dim]{name}[/dim]" + (f"  MMSI={mmsi}" if mmsi else "")
        console.print(f"[yellow]▶ tool:[/yellow]{detail}")
        tool_calls.append(name)

    with console.status("[bold green]Analyzing...", spinner="dots"):
        result = agent.investigate(query, on_text=on_text, on_tool_call=on_tool_call)

    console.print()
    console.print(Rule("[bold green]Assessment[/bold green]"))
    console.print(Markdown(result))
    console.print()


def interactive(agent: MaritimeAgent) -> None:
    console.print(
        Panel(
            "[bold]Maritime Intelligence Agent[/bold]\n"
            "[dim]Type your investigation query, or 'quit' to exit.[/dim]",
            border_style="cyan",
        )
    )

    while True:
        try:
            query = console.input("\n[bold cyan]🚢 >[/bold cyan] ").strip()
        except (EOFError, KeyboardInterrupt):
            console.print("\n[dim]Goodbye.[/dim]")
            break

        if query.lower() in ("quit", "exit", "q"):
            console.print("[dim]Goodbye.[/dim]")
            break

        if not query:
            continue

        try:
            run_query(agent, query)
        except KeyboardInterrupt:
            console.print("\n[yellow]Interrupted.[/yellow]")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Maritime Intelligence Agent — powered by Claude"
    )
    parser.add_argument(
        "--engine-url",
        default="http://localhost:8080",
        help="URL of the maritime spatial engine (default: http://localhost:8080)",
    )
    parser.add_argument(
        "--model",
        default="claude-opus-4-8",
        help="Anthropic model to use (default: claude-opus-4-8)",
    )
    parser.add_argument(
        "query",
        nargs="?",
        help="Investigation query (omit for interactive mode)",
    )
    args = parser.parse_args()

    try:
        client = EngineClient(base_url=args.engine_url)
        # Quick connectivity check
        stats = client.get_stats()
        console.print(
            f"[green]✓[/green] Connected to engine at {args.engine_url} "
            f"— [bold]{stats.get('vessel_count', 0)}[/bold] vessels tracked"
        )
    except Exception as exc:
        console.print(
            f"[red]✗ Cannot connect to engine at {args.engine_url}:[/red] {exc}\n"
            "Make sure the maritime server is running:  [bold]./build/maritime_server[/bold]",
            highlight=False,
        )
        sys.exit(1)

    agent = MaritimeAgent(engine_client=client, model=args.model)

    try:
        if args.query:
            run_query(agent, args.query)
        else:
            interactive(agent)
    finally:
        client.close()


if __name__ == "__main__":
    main()
