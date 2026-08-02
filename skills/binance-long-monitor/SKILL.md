---
name: binance-long-monitor
description: Monitor Binance USD-M perpetual public market data for long-only crash-recovery setups and produce concise Korean alerts. Use when asked to scan, watch, or find Binance futures long candidates.
---

# Binance USD-M long monitor

Run the bundled scanner rather than estimating market values:

```bash
python3 scripts/binance_long_monitor.py --once
```

For continuous monitoring, run `python3 scripts/binance_long_monitor.py --watch`; it scans immediately and then every 60 seconds. Forward each output beginning `신규 롱 후보 발생` immediately. Do not wait for a periodic summary. Avoid repeating a candidate unless its status, trigger, stop, or targets changed.

## Non-negotiable rules

- Never recommend a short.
- Never upgrade WATCH or WAIT to ENTRY_READY by judgment. Use the scanner classification.
- If BTC blocks entries, do not recommend a new long.
- Present at most three ENTRY_READY candidates.
- Retain current price, conditional trigger, stop, both targets, risk/reward, invalidation, and the 10x isolated warning.
- State that this is high-risk futures screening, not certain financial advice.
- Risk sizing is based on stop distance: risk 0.3%–0.5% of account equity per trade, stop for the day at -2%, and stop after three consecutive losses. Ten-times leverage is isolated margin, not ten-times total-account exposure.

Configuration thresholds can be inspected with `python3 scripts/binance_long_monitor.py --help`.
