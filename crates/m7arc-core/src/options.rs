use std::path::PathBuf;

#[derive(Debug, Clone)]
pub struct EngineOptions {
    /// Load external codec/format modules from the user and application plug-in directories.
    pub load_user_modules: bool,
    pub additional_module_directories: Vec<PathBuf>,
}

impl Default for EngineOptions {
    fn default() -> Self {
        EngineOptions { load_user_modules: true, additional_module_directories: Vec::new() }
    }
}

/// How an existing archive is opened.
#[derive(Debug, Clone, Default)]
pub struct OpenOptions {
    pub password: Option<String>,
    /// Force an archive type (for example `"zip"`) instead of detecting it.
    pub archive_type: Option<String>,
}

impl OpenOptions {
    pub fn with_password(password: impl Into<String>) -> Self {
        OpenOptions { password: Some(password.into()), archive_type: None }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum ExtractPathMode {
    /// Keep the stored directory structure.
    #[default]
    Full,
    /// Extract everything into the destination, without directories.
    Flat,
    Current,
    Absolute,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum OverwriteMode {
    /// Ask through the overwrite callback.
    #[default]
    Ask,
    Replace,
    Skip,
    RenameIncoming,
    RenameExisting,
}

#[derive(Debug, Clone)]
pub struct ExtractOptions {
    pub password: Option<String>,
    pub path_mode: ExtractPathMode,
    pub overwrite: OverwriteMode,
    pub eliminate_duplicate_root: bool,
    pub create_archive_folder: bool,
    pub restore_mac_metadata: bool,
    pub current_directory: Option<PathBuf>,
}

impl Default for ExtractOptions {
    fn default() -> Self {
        ExtractOptions {
            password: None,
            path_mode: ExtractPathMode::Full,
            overwrite: OverwriteMode::Ask,
            eliminate_duplicate_root: false,
            create_archive_folder: false,
            restore_mac_metadata: true,
            current_directory: None,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum UpdateMode {
    #[default]
    Add,
    Update,
    Freshen,
    Synchronize,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum ArchivePathMode {
    /// Store paths relative to the input's parent folder.
    #[default]
    Relative,
    Full,
    Absolute,
}

#[derive(Debug, Clone)]
pub struct CreateOptions {
    /// `"7z"`, `"zip"`, `"tar"`, `"wim"`, `"gzip"`, `"bzip2"`, `"xz"`, `"tar.gz"`, `"tar.bz2"` or `"tar.xz"`.
    pub format: String,
    pub level: u32,
    pub method: Option<String>,
    pub dictionary_bytes: u64,
    pub word_size: u32,
    /// 0 selects the thread count automatically.
    pub threads: u32,
    pub solid: bool,
    pub solid_block: Option<String>,
    pub password: Option<String>,
    pub encrypt_headers: bool,
    pub encryption_method: Option<String>,
    pub volume_sizes: Vec<u64>,
    pub update_mode: UpdateMode,
    pub store_symbolic_links: bool,
    pub store_hard_links: bool,
    pub delete_after_compressing: bool,
    pub preserve_access_time: bool,
    pub preserve_mac_metadata: bool,
    pub archive_prefix: Option<String>,
    pub open_shared_files: bool,
    pub set_archive_mtime: bool,
    pub store_owner_id: bool,
    pub store_owner_name: bool,
    pub path_mode: ArchivePathMode,
    pub include_patterns: Vec<String>,
    pub exclude_patterns: Vec<String>,
    /// Extra `(name, value)` codec properties passed through to the format.
    pub properties: Vec<(String, String)>,
}

impl Default for CreateOptions {
    fn default() -> Self {
        CreateOptions {
            format: "7z".into(),
            level: 5,
            method: None,
            dictionary_bytes: 0,
            word_size: 0,
            threads: 0,
            solid: true,
            solid_block: None,
            password: None,
            encrypt_headers: false,
            encryption_method: None,
            volume_sizes: Vec::new(),
            update_mode: UpdateMode::Add,
            store_symbolic_links: true,
            store_hard_links: false,
            delete_after_compressing: false,
            preserve_access_time: false,
            preserve_mac_metadata: true,
            archive_prefix: None,
            open_shared_files: false,
            set_archive_mtime: false,
            store_owner_id: false,
            store_owner_name: false,
            path_mode: ArchivePathMode::Relative,
            include_patterns: Vec::new(),
            exclude_patterns: Vec::new(),
            properties: Vec::new(),
        }
    }
}

impl CreateOptions {
    pub fn with_format(format: impl Into<String>) -> Self {
        CreateOptions { format: format.into(), ..Self::default() }
    }
}
