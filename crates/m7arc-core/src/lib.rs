//! Safe Rust API over the M7Arc archive engine.
//!
//! ```no_run
//! use m7arc_core::{Callbacks, CreateOptions, Engine, ExtractOptions, OpenOptions};
//!
//! # fn main() -> m7arc_core::Result<()> {
//! let engine = Engine::new()?;
//! let mut callbacks = Callbacks::new().on_progress(|p| {
//!     eprintln!("{:?} {}", p.stage, p.path);
//!     true // return false to cancel
//! });
//! engine.create(
//!     &["/tmp/photos"],
//!     "/tmp/photos.7z",
//!     &CreateOptions::default(),
//!     &mut callbacks,
//! )?;
//! let archive = engine.list("/tmp/photos.7z", &OpenOptions::default(), &mut Callbacks::new())?;
//! println!("{} entries", archive.entries.len());
//! engine.extract(
//!     "/tmp/photos.7z",
//!     "/tmp/out",
//!     &[],
//!     &ExtractOptions::default(),
//!     &mut Callbacks::new(),
//! )?;
//! # Ok(())
//! # }
//! ```
//!
//! An [`Engine`] belongs to one serial worker: it is `Send` but not `Sync`, and
//! callbacks run one at a time, possibly on an engine-internal thread, which is
//! why they must be `Send`. A panic inside a callback cancels the operation and
//! is re-raised on the calling thread once the engine returns.

mod callbacks;
mod engine;
mod error;
mod options;
mod types;

pub use callbacks::{
    Callbacks, CancelToken, MemoryAnswer, MemoryRequest, OverwriteAnswer, OverwriteRequest, Progress, Stage,
};
pub use engine::Engine;
pub use error::{Error, Result};
pub use options::{
    ArchivePathMode, CreateOptions, EngineOptions, ExtractOptions, ExtractPathMode, OpenOptions, OverwriteMode,
    UpdateMode,
};
pub use types::{
    Archive, Entry, FileHash, FormatInfo, HashSummary, HashTotal, HasherInfo, ManifestEntry, ManifestReport,
    OperationReport, Property, VerifyStatus,
};
