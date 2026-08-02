#!/usr/bin/env python3
"""Long-only Binance USD-M crash-recovery scanner using public endpoints."""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Any

BASE_URL = "https://fapi.binance.com"
TRADFI_TOKENS = {
    "SOXL", "SOXS", "MU", "SAMSUNG", "BABA", "QCOM", "IBM", "CRWD",
    "XLE", "HPE", "ARM", "WDC", "OPENAI",
}


class BinanceClient:
    def __init__(self, base_url: str = BASE_URL, timeout: float = 15.0) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout

    def get(self, path: str, **params: Any) -> Any:
        query = urllib.parse.urlencode(params)
        url = f"{self.base_url}{path}" + (f"?{query}" if query else "")
        request = urllib.request.Request(url, headers={"User-Agent": "binance-long-monitor/0.1"})
        with urllib.request.urlopen(request, timeout=self.timeout) as response:
            return json.load(response)


def ema(values: list[float], period: int) -> float:
    value = sum(values[:period]) / period
    multiplier = 2 / (period + 1)
    for item in values[period:]:
        value = item * multiplier + value * (1 - multiplier)
    return value


def rsi(values: list[float], period: int = 14) -> float:
    changes = [b - a for a, b in zip(values, values[1:])]
    gains = [max(change, 0.0) for change in changes]
    losses = [max(-change, 0.0) for change in changes]
    average_gain = sum(gains[:period]) / period
    average_loss = sum(losses[:period]) / period
    for gain, loss in zip(gains[period:], losses[period:]):
        average_gain = (average_gain * (period - 1) + gain) / period
        average_loss = (average_loss * (period - 1) + loss) / period
    if average_loss == 0:
        return 100.0
    return 100 - 100 / (1 + average_gain / average_loss)


def atr(rows: list[list[Any]], period: int = 14) -> float:
    highs = [float(row[2]) for row in rows]
    lows = [float(row[3]) for row in rows]
    closes = [float(row[4]) for row in rows]
    true_ranges = [
        max(highs[i] - lows[i], abs(highs[i] - closes[i - 1]), abs(lows[i] - closes[i - 1]))
        for i in range(1, len(rows))
    ]
    value = sum(true_ranges[:period]) / period
    for item in true_ranges[period:]:
        value = (value * (period - 1) + item) / period
    return value


def approximate_vwap(rows: list[list[Any]], bars: int = 96) -> float:
    recent = rows[-bars:]
    weighted = sum(((float(r[2]) + float(r[3]) + float(r[4])) / 3) * float(r[5]) for r in recent)
    volume = sum(float(r[5]) for r in recent)
    return weighted / volume if volume else math.nan


def successive_new_lows(rows: list[list[Any]]) -> bool:
    """Block when the latest two completed 3-bar swing windows both set lower lows."""
    lows = [float(row[3]) for row in rows[:-1]]
    if len(lows) < 7:
        return False
    windows = [min(lows[-7:-4]), min(lows[-5:-2]), min(lows[-3:])]
    return windows[0] > windows[1] > windows[2]


def tradfi_symbol(symbol: str) -> bool:
    base = symbol.removesuffix("USDT")
    return base in TRADFI_TOKENS


def format_price(price: float) -> str:
    if price >= 1000:
        return f"{price:,.2f}"
    if price >= 1:
        return f"{price:.4f}".rstrip("0").rstrip(".")
    return f"{price:.8f}".rstrip("0").rstrip(".")


@dataclass
class Candidate:
    symbol: str
    status: str
    current: float
    trigger: float
    stop: float
    tp1: float
    tp2: float
    rr: float
    recovery_price: float
    reason: str
    score: float


def evaluate(symbol: str, ticker: dict[str, Any], funding: float, rows: list[list[Any]]) -> Candidate:
    closes = [float(row[4]) for row in rows]
    current = closes[-1]
    ema9 = ema(closes, 9)
    ema20 = ema(closes, 20)
    vwap = approximate_vwap(rows)
    rsi14 = rsi(closes)
    atr14 = atr(rows)
    six_low = min(float(row[3]) for row in rows[-6:])
    rebound = current / six_low - 1
    trigger = max(current, max(float(row[2]) for row in rows[-3:-1]) * 1.0005)
    swing_low = min(float(row[3]) for row in rows[-12:])
    stop = min(swing_low * 0.998, trigger - 1.2 * atr14)
    risk = trigger - stop
    day_high = float(ticker["highPrice"])
    room_rr = (day_high - trigger) / risk if risk > 0 else -1
    conditions = {
        "EMA9": current > ema9,
        "VWAP": current > vwap,
        "RSI35": rsi14 >= 35,
        "funding": funding <= 0.001,
        "rebound": rebound >= 0.02,
        "RR3.5": room_rr >= 3.5,
    }
    ready = all(conditions.values())
    missing = [name for name, passed in conditions.items() if not passed]
    status = "ENTRY_READY" if ready else "WATCH" if len(missing) <= 2 and conditions["rebound"] else "WAIT"
    recovery = max(ema9, vwap, trigger if not conditions["EMA9"] else 0)
    tp1 = trigger + 1.25 * risk
    tp2 = trigger + 3.5 * risk
    reason = (
        f"RSI {rsi14:.1f}, EMA9 {format_price(ema9)}, EMA20 {format_price(ema20)}, "
        f"VWAP {format_price(vwap)}, 6봉 반등 {rebound * 100:.1f}%, 펀딩 {funding * 100:.4f}%"
    )
    if missing:
        reason += "; 부족: " + ", ".join(missing)
    score = sum(conditions.values()) + min(max(room_rr, 0), 10) / 10
    return Candidate(symbol, status, current, trigger, stop, tp1, tp2, 3.5, recovery, reason, score)


def scan(client: BinanceClient) -> str:
    exchange = client.get("/fapi/v1/exchangeInfo")
    allowed = {
        item["symbol"] for item in exchange["symbols"]
        if item.get("contractType") == "PERPETUAL"
        and item.get("quoteAsset") == "USDT"
        and item.get("status") == "TRADING"
        and not tradfi_symbol(item["symbol"])
    }
    tickers = client.get("/fapi/v1/ticker/24hr")
    ticker_by_symbol = {item["symbol"]: item for item in tickers if item["symbol"] in allowed}
    premiums = client.get("/fapi/v1/premiumIndex")
    funding = {item["symbol"]: float(item.get("lastFundingRate", 0)) for item in premiums}
    btc = ticker_by_symbol["BTCUSDT"]
    btc_change = float(btc["priceChangePercent"])
    btc_price = float(btc["lastPrice"])
    btc_funding = funding.get("BTCUSDT", 0.0)
    btc15 = client.get("/fapi/v1/klines", symbol="BTCUSDT", interval="15m", limit=40)
    btc1h = client.get("/fapi/v1/klines", symbol="BTCUSDT", interval="1h", limit=40)
    btc_block = btc_change <= -2.5 or successive_new_lows(btc15) or successive_new_lows(btc1h)

    eligible: list[tuple[str, dict[str, Any]]] = []
    for symbol, ticker in ticker_by_symbol.items():
        if symbol == "BTCUSDT":
            continue
        high, low, current = map(float, (ticker["highPrice"], ticker["lowPrice"], ticker["lastPrice"]))
        if low <= 0 or high <= 0:
            continue
        range_pct = (high - low) / low
        drawdown = (high - current) / high
        rebound = (current - low) / low
        if float(ticker["quoteVolume"]) >= 20_000_000 and range_pct >= 0.25 and 0.10 <= drawdown <= 0.45 and rebound > 0:
            eligible.append((symbol, ticker))

    candidates: list[Candidate] = []
    for symbol, ticker in sorted(eligible, key=lambda item: float(item[1]["quoteVolume"]), reverse=True)[:30]:
        rows = client.get("/fapi/v1/klines", symbol=symbol, interval="15m", limit=100)
        candidate = evaluate(symbol, ticker, funding.get(symbol, 0.0), rows)
        if btc_block and candidate.status == "ENTRY_READY":
            candidate.status = "WATCH"
            candidate.reason += "; BTC 신규 롱 차단"
        candidates.append(candidate)

    timestamp = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    header = f"BTCUSDT {format_price(btc_price)} | 24h {btc_change:+.2f}% | 펀딩 {btc_funding * 100:+.4f}% | {timestamp}"
    ready = sorted((c for c in candidates if c.status == "ENTRY_READY"), key=lambda c: c.score, reverse=True)[:3]
    if ready:
        lines = ["신규 롱 후보 발생", header]
        for c in ready:
            lines.extend([
                f"- {c.symbol} ENTRY_READY | 현재가 {format_price(c.current)}",
                f"  조건부 진입: {format_price(c.trigger)} 이상 15m 유지 | 손절 {format_price(c.stop)}",
                f"  1차 익절 {format_price(c.tp1)} (1.25R), 2차 익절 {format_price(c.tp2)} ({c.rr:.1f}R) | 예상 손익비 1:{c.rr:.1f}",
                "  1차 익절 후 스탑을 본전으로 이동.",
                f"  무효: 15m 종가가 {format_price(c.stop)} 아래 또는 BTC 차단 조건 발생. {c.reason}",
                "  10배 isolated 주의: 손절 거리로 수량을 산정해 계좌의 0.3%~0.5%만 위험에 노출(계좌 전체 10배 사용 금지).",
            ])
    else:
        lines = ["현재 신규 롱 후보 없음", header]
        watch = sorted((c for c in candidates if c.status == "WATCH"), key=lambda c: c.score, reverse=True)[:3]
        for c in watch:
            lines.append(f"- WATCH {c.symbol}: {format_price(c.recovery_price)} 회복 필요 | {c.reason}")
        if btc_block:
            lines.append("- BTC 조건 차단: 24h -2.5% 이하 또는 15m/1h 새 저점 연속 갱신 중.")
    lines.append("고위험 선물 스크리닝이며 확정적 재정 조언이 아닙니다. 일 -2% 또는 3연속 손절 시 중단.")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--once", action="store_true", help="scan once (default)")
    mode.add_argument("--watch", action="store_true", help="scan immediately and every 60 seconds")
    parser.add_argument("--interval", type=int, default=60, help="watch interval in seconds (default: 60)")
    parser.add_argument("--base-url", default=BASE_URL, help="Binance-compatible API base URL")
    args = parser.parse_args()
    client = BinanceClient(args.base_url)
    while True:
        try:
            print(scan(client), flush=True)
        except (KeyError, ValueError, urllib.error.URLError, TimeoutError) as error:
            print(f"시장 데이터 조회 실패: {error}", file=sys.stderr, flush=True)
            if not args.watch:
                return 1
        if not args.watch:
            return 0
        time.sleep(max(args.interval, 1))


if __name__ == "__main__":
    raise SystemExit(main())
