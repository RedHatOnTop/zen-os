// zen-test — Global timeout watchdog.
//
// Wraps any async operation with a deadline. If the deadline expires,
// returns Err so the caller can emit {"status":"timeout"} and exit 2.

use std::future::Future;
use std::time::Duration;
use tokio::time;

/// Error returned when the timeout expires.
#[derive(Debug, thiserror::Error)]
pub enum TimeoutError<E> {
    #[error("operation timed out after {0}s")]
    TimedOut(u64),
    #[error(transparent)]
    Inner(E),
}

/// Run an async operation with a global timeout.
///
/// If `timeout_secs` is 0, no timeout is applied.
pub async fn with_timeout<F, T, E>(
    timeout_secs: u64,
    f: F,
) -> Result<T, TimeoutError<E>>
where
    F: Future<Output = Result<T, E>>,
{
    if timeout_secs == 0 {
        return f.await.map_err(TimeoutError::Inner);
    }

    match time::timeout(Duration::from_secs(timeout_secs), f).await {
        Ok(Ok(val)) => Ok(val),
        Ok(Err(e)) => Err(TimeoutError::Inner(e)),
        Err(_) => Err(TimeoutError::TimedOut(timeout_secs)),
    }
}

/// Simple deadline-based sleep check: returns true if the deadline has passed.
pub fn deadline_expired(start: std::time::Instant, timeout_secs: u64) -> bool {
    if timeout_secs == 0 {
        return false;
    }
    start.elapsed() >= Duration::from_secs(timeout_secs)
}
