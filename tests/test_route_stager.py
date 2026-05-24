"""Unit tests for the gesture-gated staging state machine."""

from control_plane.stager import RouteStager


class FakeClock:
    def __init__(self):
        self.t = 1000.0

    def __call__(self):
        return self.t


def make(ttl=60.0):
    calls = []
    clk = FakeClock()
    st = RouteStager(route_fn=lambda n, t: calls.append((n, t)), ttl_s=ttl, clock=clk)
    return st, calls, clk


def test_stage_then_confirm_routes_once():
    st, calls, _ = make()
    st.stage(2, "run the tests")
    assert st.confirm() is True
    assert calls == [(2, "run the tests")]
    # pending cleared — a second confirm does nothing
    assert st.confirm() is False
    assert calls == [(2, "run the tests")]


def test_stage_then_cancel_never_routes():
    st, calls, _ = make()
    st.stage(1, "rm -rf /")
    assert st.cancel() is True
    assert calls == []
    assert st.confirm() is False
    assert calls == []


def test_confirm_without_pending_is_noop():
    st, calls, _ = make()
    assert st.confirm() is False
    assert calls == []


def test_cancel_without_pending_returns_false():
    st, _, _ = make()
    assert st.cancel() is False


def test_stage_is_last_wins():
    st, calls, _ = make()
    st.stage(1, "first")
    st.stage(3, "second")
    st.confirm()
    assert calls == [(3, "second")]


def test_ttl_expiry_clears_pending():
    st, calls, clk = make(ttl=60.0)
    st.stage(2, "stale")
    clk.t += 61.0  # past TTL
    assert st.peek() is None
    assert st.confirm() is False
    assert calls == []


def test_within_ttl_still_fires():
    st, calls, clk = make(ttl=60.0)
    st.stage(2, "fresh")
    clk.t += 59.0
    assert st.confirm() is True
    assert calls == [(2, "fresh")]


def test_restage_resets_ttl():
    st, calls, clk = make(ttl=60.0)
    st.stage(1, "a")
    clk.t += 50.0
    st.stage(1, "b")     # resets staged_at
    clk.t += 50.0        # 100s since first stage, 50s since restage
    assert st.confirm() is True
    assert calls == [(1, "b")]


def test_route_runs_outside_lock_reentrant():
    # route_fn re-enters the stager (stage + peek). If confirm() held the lock
    # across the route call this would deadlock — proves the route runs lock-free.
    from control_plane.stager import RouteStager

    seen = []
    st = RouteStager(route_fn=lambda n, t: (seen.append((n, t)), st.stage(99, "next"), st.peek()))
    st.stage(1, "go")
    assert st.confirm() is True          # would hang if route held the lock
    assert seen == [(1, "go")]
    assert st.peek().number == 99        # the re-entrant stage took effect


def test_concurrent_confirm_and_stage_no_crash():
    # Hammer stage/confirm/cancel from threads; must not raise or deadlock.
    import threading
    from control_plane.stager import RouteStager

    fired = []
    st = RouteStager(route_fn=lambda n, t: fired.append((n, t)), ttl_s=999)

    def worker(i):
        for _ in range(200):
            st.stage(i, "x")
            st.confirm()
            st.cancel()

    threads = [threading.Thread(target=worker, args=(i,)) for i in range(4)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    # No assertion on counts (inherently racy) — the point is no crash/deadlock.
    assert isinstance(fired, list)
