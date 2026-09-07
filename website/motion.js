(() => {
  const root = document.documentElement;
  const scene = document.querySelector('.hero-visual');
  const toggle = document.querySelector('#motion-toggle');
  const label = toggle.querySelector('.motion-toggle-label');
  const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');
  const precisePointer = matchMedia('(hover: hover) and (pointer: fine)');
  const reveals = [...document.querySelectorAll('[data-reveal]')];
  let userPaused = null;
  let paused = reducedMotion.matches;
  let sceneVisible = true;
  let frame = 0;
  let pendingPoint = null;

  const canTrack = () => !paused && !reducedMotion.matches
    && precisePointer.matches && sceneVisible && !document.hidden;

  function resetPointer() {
    cancelAnimationFrame(frame);
    frame = 0;
    pendingPoint = null;
    for (const name of ['--tilt-x', '--tilt-y', '--pointer-x', '--pointer-y', '--scene-x', '--scene-y']) {
      scene.style.removeProperty(name);
    }
  }

  function updatePointer() {
    frame = 0;
    if (!pendingPoint || !canTrack()) return;
    const bounds = scene.getBoundingClientRect();
    const x = Math.max(-1, Math.min(1, (pendingPoint.x - bounds.left) / bounds.width * 2 - 1));
    const y = Math.max(-1, Math.min(1, (pendingPoint.y - bounds.top) / bounds.height * 2 - 1));
    scene.style.setProperty('--tilt-x', `${(-y * 10).toFixed(2)}deg`);
    scene.style.setProperty('--tilt-y', `${(x * 12).toFixed(2)}deg`);
    scene.style.setProperty('--pointer-x', `${((x + 1) * 50).toFixed(2)}%`);
    scene.style.setProperty('--pointer-y', `${((y + 1) * 50).toFixed(2)}%`);
    scene.style.setProperty('--scene-x', `${(x * 12).toFixed(2)}px`);
    scene.style.setProperty('--scene-y', `${(y * 10).toFixed(2)}px`);
    pendingPoint = null;
  }

  scene.addEventListener('pointermove', (event) => {
    if (!canTrack() || event.pointerType === 'touch') return;
    pendingPoint = { x: event.clientX, y: event.clientY };
    if (!frame) frame = requestAnimationFrame(updatePointer);
  }, { passive: true });
  scene.addEventListener('pointerleave', resetPointer);
  scene.addEventListener('pointercancel', resetPointer);

  const revealObserver = new IntersectionObserver((entries) => {
    for (const entry of entries) {
      if (entry.isIntersecting) reveal(entry.target);
    }
  }, { threshold: 0.08, rootMargin: '0px 0px -24px 0px' });

  function reveal(element) {
    element.classList.remove('reveal-pending');
    element.classList.add('is-visible');
    revealObserver.unobserve(element);
  }

  function syncMotion() {
    paused = userPaused ?? reducedMotion.matches;
    root.dataset.motion = paused ? 'paused' : 'active';
    toggle.setAttribute('aria-pressed', String(paused));
    label.textContent = paused ? '开启动效' : '暂停动效';
    if (!canTrack()) resetPointer();
    if (paused || reducedMotion.matches) reveals.forEach(reveal);
  }

  function syncVisibility() {
    root.dataset.pageHidden = String(document.hidden);
    scene.dataset.resting = String(!sceneVisible || document.hidden);
    if (!canTrack()) resetPointer();
  }

  toggle.addEventListener('click', () => {
    userPaused = !paused;
    syncMotion();
  });
  reducedMotion.addEventListener('change', syncMotion);
  precisePointer.addEventListener('change', () => {
    if (!canTrack()) resetPointer();
  });
  document.addEventListener('visibilitychange', syncVisibility);

  syncMotion();
  for (const element of reveals) {
    if (!paused && !reducedMotion.matches && element.getBoundingClientRect().top >= innerHeight) {
      element.classList.add('reveal-pending');
      revealObserver.observe(element);
    } else {
      reveal(element);
    }
  }

  const sceneBounds = scene.getBoundingClientRect();
  sceneVisible = sceneBounds.bottom > 0 && sceneBounds.top < innerHeight;
  syncVisibility();
  new IntersectionObserver(([entry]) => {
    sceneVisible = entry.isIntersecting;
    syncVisibility();
  }).observe(scene);
  toggle.hidden = false;
})();
