/// A format registered in the engine.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FormatInfo {
    pub name: String,
    pub extensions: Vec<String>,
    pub can_read: bool,
    pub can_write: bool,
    pub is_hash: bool,
}

/// A checksum algorithm registered in the engine.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HasherInfo {
    pub name: String,
    pub digest_size: u32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Property {
    pub id: u32,
    pub name: String,
    pub value: String,
}

/// One member of a listed archive.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Entry {
    /// Index to pass to extract/test/remove/rename for this listing.
    pub index: u32,
    pub path: String,
    pub size: u64,
    pub packed_size: u64,
    pub is_directory: bool,
    pub encrypted: bool,
    pub alternate_stream: bool,
    pub modified_unix_seconds: i64,
    pub method: String,
    pub attributes: String,
    pub properties: Vec<Property>,
}

/// Snapshot of an archive produced by [`Engine::list`](crate::Engine::list).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Archive {
    pub path: String,
    pub format: String,
    pub can_update: bool,
    pub can_set_comment: bool,
    pub entries: Vec<Entry>,
    pub properties: Vec<Property>,
    pub volumes: Vec<String>,
}

impl Archive {
    /// The entry whose path is exactly `path`.
    pub fn find(&self, path: &str) -> Option<&Entry> {
        self.entries.iter().find(|e| e.path == path)
    }
}

/// Outcome of extract, test, create and archive-editing operations.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct OperationReport {
    pub completed_items: u64,
    pub output_paths: Vec<String>,
    /// Per-member error messages; empty on success.
    pub errors: Vec<String>,
    pub password_may_be_incorrect: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FileHash {
    pub path: String,
    pub algorithm: String,
    pub digest: String,
    pub size: u64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HashTotal {
    pub algorithm: String,
    pub data_sum: String,
    pub data_and_names_sum: String,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct HashSummary {
    pub files: Vec<FileHash>,
    pub totals: Vec<HashTotal>,
    pub file_count: u64,
    pub directory_count: u64,
    pub total_bytes: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum VerifyStatus {
    Passed,
    Missing,
    Mismatch,
    Error,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ManifestEntry {
    pub path: String,
    pub status: VerifyStatus,
    pub message: String,
}

/// Result of checking the files named by a checksum manifest.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ManifestReport {
    pub entries: Vec<ManifestEntry>,
    pub errors: Vec<String>,
}

impl ManifestReport {
    /// True when every entry passed and no error was recorded.
    pub fn ok(&self) -> bool {
        self.errors.is_empty() && self.entries.iter().all(|e| e.status == VerifyStatus::Passed)
    }
}
