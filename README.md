# Binance Long Monitor

A dependency-free Codex plugin and CLI that scans Binance public USD-M futures data for **long-only** recovery setups. It scans immediately in one-shot mode or once per minute in watch mode; it does not wait for a 15-minute digest.

## Run

```bash
python3 scripts/binance_long_monitor.py --once
python3 scripts/binance_long_monitor.py --watch
```

No API key is required. The scanner uses Binance USD-M public `exchangeInfo`, 24-hour ticker, premium index, and 15-minute/1-hour kline endpoints. Network/API failures produce a short error and a non-zero exit in one-shot mode; watch mode retries on the next interval.

## Strategy constraints

- USDT-settled perpetuals only; known TradFi-style contracts are excluded.
- Minimum 20M USDT 24-hour quote volume, 25% high/low range, 10%–45% pullback from the high, and a rebound from the low.
- ENTRY_READY requires recovery above EMA9 and approximate 24-hour VWAP, RSI14 at least 35, non-overheated funding, and room to the 24-hour high for at least 3.5R.
- BTC at or below -2.5% over 24 hours, or making successive new lows on 15-minute/1-hour structure, blocks every new entry.
- Stops use the recent swing low and ATR; TP1 is 1.25R and TP2 is 3.5R. Move the stop to breakeven after TP1.

This is a high-risk futures market screener, not financial advice. Position size from the stop so one loss is 0.3%–0.5% of account equity, cap daily losses at 2%, and stop after three consecutive losses. Use 10x leverage only as isolated margin—not on the full account.
