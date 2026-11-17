use crate::types::OperationReport;
use std::fmt;

pub type Result<T> = std::result::Result<T, Error>;

/// Failure of an engine call.
#[derive(Debug)]
pub enum Error {
    /// The operation was cancelled through a callback or a [`CancelToken`](crate::CancelToken).
    Cancelled,
    /// The archive needs a password and none was supplied.
    PasswordRequired(String),
    /// A password was supplied but the archive could not be read with it, or it is damaged.
    PasswordOrCorruption(String),
    /// An argument was rejected before reaching the engine.
    InvalidArgument(String),
    /// The operation ran but some members failed; the report lists them.
    OperationFailed(OperationReport),
    /// Any other engine failure (unreadable file, unknown format, ...).
    Engine(String),
    /// An unexpected failure inside the engine library.
    Internal(String),
}

impl Error {
    /// True when retrying with another password may succeed.
    pub fn is_password_error(&self) -> bool {
        match self {
            Error::PasswordRequired(_) | Error::PasswordOrCorruption(_) => true,
            Error::OperationFailed(report) => report.password_may_be_incorrect || report.needs_password(),
            _ => false,
        }
    }

    pub fn is_cancelled(&self) -> bool {
        matches!(self, Error::Cancelled)
    }

    pub(crate) fn from_status(status: i32, message: String) -> Error {
        use m7arc_sys as sys;
        match status {
            sys::M7ARC_ERR_CANCELLED => Error::Cancelled,
            sys::M7ARC_ERR_PASSWORD_REQUIRED => Error::PasswordRequired(message),
            sys::M7ARC_ERR_PASSWORD_OR_CORRUPTION => Error::PasswordOrCorruption(message),
            sys::M7ARC_ERR_INVALID_ARGUMENT => Error::InvalidArgument(message),
            sys::M7ARC_ERR_INTERNAL => Error::Internal(message),
            _ => Error::Engine(message),
        }
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Cancelled => f.write_str("operation cancelled"),
            Error::PasswordRequired(m) | Error::PasswordOrCorruption(m) | Error::InvalidArgument(m) => {
                f.write_str(m)
            }
            Error::Engine(m) | Error::Internal(m) => f.write_str(m),
            Error::OperationFailed(report) => match report.errors.first() {
                Some(first) if report.errors.len() > 1 => {
                    write!(f, "{first} (and {} more errors)", report.errors.len() - 1)
                }
                Some(first) => f.write_str(first),
                None => f.write_str("operation failed"),
            },
        }
    }
}

impl std::error::Error for Error {}
