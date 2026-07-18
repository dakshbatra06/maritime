"""Maritime intelligence agent using the Anthropic SDK tool-use loop."""
from __future__ import annotations

import os
from typing import Any

import anthropic

from engine_client import EngineClient
from tools import TOOL_DEFINITIONS, dispatch

SYSTEM_PROMPT = """\
You are a maritime intelligence analyst with direct access to a live vessel tracking system.

Your role is to investigate maritime situations, detect suspicious behaviour, and produce
clear, actionable intelligence assessments.

Investigation methodology:
1. Start broad — check fleet stats, then scan for fleet-wide anomalies.
2. Drill down on suspicious vessels — get their current info, run anomaly checks, \
review track history.
3. Check context — look for nearby vessels, possible rendezvous patterns, correlated movement.
4. Look for these maritime red flags:
   - AIS dark periods: vessels that stop transmitting (intentional transponder disabling).
   - Speed anomalies: reported speed jumps inconsistent with vessel class.
   - Teleportation: impossible position jumps implying GPS spoofing or data corruption.
   - Loitering: vessel circling in an unexpected area (waiting for a rendezvous?).
   - Proximity clustering: multiple vessels meeting in an unusual location.

Always explain your reasoning step by step. When you find something suspicious, investigate
further before concluding. End every investigation with a structured assessment:
  • Summary of findings
  • Vessels of concern (MMSI, name, reason)
  • Risk level: LOW / MEDIUM / HIGH / CRITICAL
  • Recommended follow-up actions
"""


class MaritimeAgent:
    """Wraps the Anthropic tool-use loop for maritime investigation queries."""

    def __init__(
        self,
        engine_client: EngineClient,
        model: str = "claude-opus-4-8",
        max_tokens: int = 16_000,
        max_iterations: int = 20,
    ) -> None:
        self._engine = engine_client
        self._model = model
        self._max_tokens = max_tokens
        self._max_iterations = max_iterations
        self._anthropic = anthropic.Anthropic(
            api_key=os.environ.get("ANTHROPIC_API_KEY")
        )

    def investigate(
        self,
        query: str,
        on_text: Any = None,
        on_tool_call: Any = None,
    ) -> str:
        """Run an investigation query.

        Args:
            query: Natural-language investigation question.
            on_text: Optional callback(text: str) called as Claude produces text.
            on_tool_call: Optional callback(name: str, input: dict) on each tool use.

        Returns:
            The final text response from Claude.
        """
        messages: list[dict] = [{"role": "user", "content": query}]
        final_text = ""

        for _ in range(self._max_iterations):
            response = self._anthropic.messages.create(
                model=self._model,
                max_tokens=self._max_tokens,
                thinking={"type": "adaptive"},
                system=SYSTEM_PROMPT,
                tools=TOOL_DEFINITIONS,
                messages=messages,
            )

            # Collect the assistant turn
            assistant_content = response.content
            messages.append({"role": "assistant", "content": assistant_content})

            # Extract any text blocks and surface them
            for block in assistant_content:
                if block.type == "text":
                    final_text = block.text
                    if on_text:
                        on_text(block.text)

            if response.stop_reason == "end_turn":
                break

            if response.stop_reason == "tool_use":
                tool_results = []
                for block in assistant_content:
                    if block.type == "tool_use":
                        if on_tool_call:
                            on_tool_call(block.name, block.input)
                        result = dispatch(self._engine, block.name, block.input)
                        tool_results.append(
                            {
                                "type": "tool_result",
                                "tool_use_id": block.id,
                                "content": result,
                            }
                        )
                messages.append({"role": "user", "content": tool_results})
            else:
                # Unexpected stop reason — surface and exit
                break

        return final_text
