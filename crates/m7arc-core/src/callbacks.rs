use m7arc_sys as sys;
use std::any::Any;
use std::ffi::{c_void, CStr, CString};
use std::os::raw::c_char;
use std::panic::{catch_unwind, resume_unwind, AssertUnwindSafe};
use std::sync::{Arc, Mutex, MutexGuard};

/// Coarse phase of a running operation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Stage {
    None,
    Scanning,
    Opening,
    Compressing,
    Committing,
    Testing,
    Extracting,
    RestoringMacMetadata,
    PreservingMacMetadata,
    Updating,
    Hashing,
    Benchmark,
    BenchmarkCompressing,
    BenchmarkDecompressing,
}

impl Stage {
    fn from_raw(value: i32) -> Stage {
        match value {
            sys::M7ARC_STAGE_SCANNING => Stage::Scanning,
            sys::M7ARC_STAGE_OPENING => Stage::Opening,
            sys::M7ARC_STAGE_COMPRESSING => Stage::Compressing,
            sys::M7ARC_STAGE_COMMITTING => Stage::Committing,
            sys::M7ARC_STAGE_TESTING => Stage::Testing,
            sys::M7ARC_STAGE_EXTRACTING => Stage::Extracting,
            sys::M7ARC_STAGE_RESTORING_MAC_METADATA => Stage::RestoringMacMetadata,
            sys::M7ARC_STAGE_PRESERVING_MAC_METADATA => Stage::PreservingMacMetadata,
            sys::M7ARC_STAGE_UPDATING => Stage::Updating,
            sys::M7ARC_STAGE_HASHING => Stage::Hashing,
            sys::M7ARC_STAGE_BENCHMARK => Stage::Benchmark,
            sys::M7ARC_STAGE_BENCHMARK_COMPRESSING => Stage::BenchmarkCompressing,
            sys::M7ARC_STAGE_BENCHMARK_DECOMPRESSING => Stage::BenchmarkDecompressing,
            _ => Stage::None,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Progress {
    pub stage: Stage,
    /// English display text for the stage.
    pub phase: String,
    /// Member or file currently processed; may be empty.
    pub path: String,
    pub completed: u64,
    pub total: u64,
    pub total_known: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct OverwriteRequest {
    pub existing_path: String,
    pub incoming_path: String,
    pub existing_size: Option<u64>,
    pub incoming_size: Option<u64>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum OverwriteAnswer {
    Replace,
    ReplaceAll,
    Skip,
    SkipAll,
    RenameIncoming,
    Cancel,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MemoryRequest {
    pub path: String,
    pub required_bytes: u64,
    pub allowed_bytes: u64,
    pub report_only: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MemoryAnswer {
    Continue,
    Cancel,
}

struct TokenHandle(*mut sys::m7arc_cancel_token);
// The token is an atomic flag on the C side.
unsafe impl Send for TokenHandle {}
unsafe impl Sync for TokenHandle {}
impl Drop for TokenHandle {
    fn drop(&mut self) {
        unsafe { sys::m7arc_cancel_token_free(self.0) }
    }
}

/// A flag that cancels running operations. Clones share the flag; `cancel`
/// may be called from any thread, for example a signal handler thread.
#[derive(Clone)]
pub struct CancelToken(Arc<TokenHandle>);

impl CancelToken {
    pub fn new() -> CancelToken {
        let raw = unsafe { sys::m7arc_cancel_token_new() };
        assert!(!raw.is_null(), "out of memory creating a cancel token");
        CancelToken(Arc::new(TokenHandle(raw)))
    }

    pub fn cancel(&self) {
        unsafe { sys::m7arc_cancel_token_cancel(self.0 .0) }
    }
}

impl Default for CancelToken {
    fn default() -> Self {
        Self::new()
    }
}

impl std::fmt::Debug for CancelToken {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("CancelToken")
    }
}

type ProgressFn<'a> = Box<dyn FnMut(&Progress) -> bool + Send + 'a>;
type CancelFn<'a> = Box<dyn FnMut() -> bool + Send + 'a>;
type PasswordFn<'a> = Box<dyn FnMut() -> Option<String> + Send + 'a>;
type OverwriteFn<'a> = Box<dyn FnMut(&OverwriteRequest) -> OverwriteAnswer + Send + 'a>;
type MemoryFn<'a> = Box<dyn FnMut(&MemoryRequest) -> MemoryAnswer + Send + 'a>;

/// Closures the engine calls while an operation runs. Every member is optional.
#[derive(Default)]
pub struct Callbacks<'a> {
    progress: Option<ProgressFn<'a>>,
    cancelled: Option<CancelFn<'a>>,
    password: Option<PasswordFn<'a>>,
    overwrite: Option<OverwriteFn<'a>>,
    memory_request: Option<MemoryFn<'a>>,
    token: Option<CancelToken>,
    password_overrides: Vec<(String, String)>,
}

impl<'a> Callbacks<'a> {
    pub fn new() -> Self {
        Self::default()
    }

    /// Called with each progress update; return `false` to cancel.
    pub fn on_progress(mut self, f: impl FnMut(&Progress) -> bool + Send + 'a) -> Self {
        self.progress = Some(Box::new(f));
        self
    }

    /// Polled frequently; return `true` to cancel.
    pub fn on_cancel_check(mut self, f: impl FnMut() -> bool + Send + 'a) -> Self {
        self.cancelled = Some(Box::new(f));
        self
    }

    /// Supplies the password when the archive needs one; `None` cancels.
    pub fn on_password(mut self, f: impl FnMut() -> Option<String> + Send + 'a) -> Self {
        self.password = Some(Box::new(f));
        self
    }

    /// Decides what to do when an extracted file already exists (see [`OverwriteMode::Ask`](crate::OverwriteMode)).
    pub fn on_overwrite(mut self, f: impl FnMut(&OverwriteRequest) -> OverwriteAnswer + Send + 'a) -> Self {
        self.overwrite = Some(Box::new(f));
        self
    }

    /// Decides whether to continue when a codec asks for more memory than allowed.
    pub fn on_memory_request(mut self, f: impl FnMut(&MemoryRequest) -> MemoryAnswer + Send + 'a) -> Self {
        self.memory_request = Some(Box::new(f));
        self
    }

    pub fn cancel_token(mut self, token: &CancelToken) -> Self {
        self.token = Some(token.clone());
        self
    }

    /// Password to try for the archive at `archive`, before asking the password callback.
    pub fn password_for(mut self, archive: impl Into<String>, password: impl Into<String>) -> Self {
        self.password_overrides.push((archive.into(), password.into()));
        self
    }

    /// Runs `call` with the C callback table for these closures. A panic raised
    /// by a closure cancels the operation and is resumed here afterwards.
    pub(crate) fn with_raw<R>(&mut self, call: impl FnOnce(*const sys::m7arc_callbacks) -> R) -> R {
        let overrides: Vec<(CString, CString)> = self
            .password_overrides
            .iter()
            .filter_map(|(a, p)| Some((CString::new(a.as_str()).ok()?, CString::new(p.as_str()).ok()?)))
            .collect();
        let pairs: Vec<sys::m7arc_string_pair> =
            overrides.iter().map(|(a, p)| sys::m7arc_string_pair { first: a.as_ptr(), second: p.as_ptr() }).collect();
        let token = self.token.clone();
        let shared = Shared { callbacks: Mutex::new(self), password: Mutex::new(CString::default()), panic: Mutex::new(None) };
        let has = |present: bool| present;
        let raw = {
            let guard = lock(&shared.callbacks);
            sys::m7arc_callbacks {
                user_data: &shared as *const Shared<'_, '_> as *mut c_void,
                progress: has(guard.progress.is_some()).then_some(progress_trampoline as _),
                cancelled: has(guard.cancelled.is_some()).then_some(cancelled_trampoline as _),
                password: has(guard.password.is_some()).then_some(password_trampoline as _),
                overwrite: has(guard.overwrite.is_some()).then_some(overwrite_trampoline as _),
                memory_request: has(guard.memory_request.is_some()).then_some(memory_trampoline as _),
                cancel_token: token.as_ref().map_or(std::ptr::null(), |t| t.0 .0 as *const _),
                password_overrides: pairs.as_ptr(),
                password_override_count: pairs.len(),
            }
        };
        let result = call(&raw);
        if let Some(payload) = lock(&shared.panic).take() {
            resume_unwind(payload);
        }
        drop(token);
        result
    }
}

struct Shared<'c, 'a> {
    callbacks: Mutex<&'c mut Callbacks<'a>>,
    password: Mutex<CString>,
    panic: Mutex<Option<Box<dyn Any + Send>>>,
}

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
    mutex.lock().unwrap_or_else(|e| e.into_inner())
}

unsafe fn text(value: *const c_char) -> String {
    if value.is_null() {
        String::new()
    } else {
        CStr::from_ptr(value).to_string_lossy().into_owned()
    }
}

/// Runs a user closure without letting a panic cross the C boundary.
fn guarded<R>(user: *mut c_void, on_panic: R, f: impl FnOnce(&mut Callbacks<'_>, &Shared<'_, '_>) -> R) -> R {
    let shared = unsafe { &*(user as *const Shared<'static, 'static>) };
    if lock(&shared.panic).is_some() {
        return on_panic;
    }
    let mut callbacks = lock(&shared.callbacks);
    match catch_unwind(AssertUnwindSafe(|| f(&mut callbacks, shared))) {
        Ok(value) => value,
        Err(payload) => {
            *lock(&shared.panic) = Some(payload);
            on_panic
        }
    }
}

unsafe extern "C" fn progress_trampoline(info: *const sys::m7arc_progress_info, user: *mut c_void) -> bool {
    guarded(user, false, |callbacks, _| {
        let info = &*info;
        let progress = Progress {
            stage: Stage::from_raw(info.stage),
            phase: text(info.phase),
            path: text(info.path),
            completed: info.completed,
            total: info.total,
            total_known: info.total_known,
        };
        callbacks.progress.as_mut().map_or(true, |f| f(&progress))
    })
}

unsafe extern "C" fn cancelled_trampoline(user: *mut c_void) -> bool {
    guarded(user, true, |callbacks, _| callbacks.cancelled.as_mut().map_or(false, |f| f()))
}

unsafe extern "C" fn password_trampoline(user: *mut c_void) -> *const c_char {
    guarded(user, std::ptr::null(), |callbacks, shared| {
        let value = callbacks.password.as_mut().and_then(|f| f());
        match value.and_then(|p| CString::new(p).ok()) {
            Some(password) => {
                let mut slot = lock(&shared.password);
                *slot = password;
                slot.as_ptr()
            }
            None => std::ptr::null(),
        }
    })
}

unsafe extern "C" fn overwrite_trampoline(request: *const sys::m7arc_overwrite_request, user: *mut c_void) -> i32 {
    guarded(user, sys::M7ARC_ANSWER_CANCEL, |callbacks, _| {
        let r = &*request;
        let request = OverwriteRequest {
            existing_path: text(r.existing_path),
            incoming_path: text(r.incoming_path),
            existing_size: r.has_existing_size.then_some(r.existing_size),
            incoming_size: r.has_incoming_size.then_some(r.incoming_size),
        };
        match callbacks.overwrite.as_mut().map_or(OverwriteAnswer::Cancel, |f| f(&request)) {
            OverwriteAnswer::Replace => sys::M7ARC_ANSWER_REPLACE,
            OverwriteAnswer::ReplaceAll => sys::M7ARC_ANSWER_REPLACE_ALL,
            OverwriteAnswer::Skip => sys::M7ARC_ANSWER_SKIP,
            OverwriteAnswer::SkipAll => sys::M7ARC_ANSWER_SKIP_ALL,
            OverwriteAnswer::RenameIncoming => sys::M7ARC_ANSWER_RENAME_INCOMING,
            OverwriteAnswer::Cancel => sys::M7ARC_ANSWER_CANCEL,
        }
    })
}

unsafe extern "C" fn memory_trampoline(request: *const sys::m7arc_memory_request, user: *mut c_void) -> i32 {
    guarded(user, sys::M7ARC_MEMORY_CANCEL, |callbacks, _| {
        let r = &*request;
        let request = MemoryRequest {
            path: text(r.path),
            required_bytes: r.required_bytes,
            allowed_bytes: r.allowed_bytes,
            report_only: r.report_only,
        };
        match callbacks.memory_request.as_mut().map_or(MemoryAnswer::Cancel, |f| f(&request)) {
            MemoryAnswer::Continue => sys::M7ARC_MEMORY_CONTINUE,
            MemoryAnswer::Cancel => sys::M7ARC_MEMORY_CANCEL,
        }
    })
}
