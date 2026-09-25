// PulseAmp website: small enhancements (the page works fine without JavaScript)

// Downloads are Windows-only: point other systems at the build instructions
(function showOsNote() {
  const note = document.getElementById("os-note");
  if (!note) return;
  const platform = (navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || "";
  const isWindows = /win/i.test(platform) || /windows/i.test(navigator.userAgent);
  if (!isWindows) note.hidden = false;
})();

// Highlight the menu link of the section currently on screen
(function highlightNav() {
  const links = [...document.querySelectorAll('.nav nav a[href^="#"]')];
  const sections = links
    .map((a) => document.querySelector(a.getAttribute("href")))
    .filter(Boolean);
  if (!sections.length || !("IntersectionObserver" in window)) return;

  const observer = new IntersectionObserver(
    (entries) => {
      entries.forEach((entry) => {
        if (!entry.isIntersecting) return;
        links.forEach((a) => a.classList.toggle("active", a.getAttribute("href") === "#" + entry.target.id));
      });
    },
    { rootMargin: "-45% 0px -50% 0px" }   // "current" = the section crossing the middle of the screen
  );
  sections.forEach((s) => observer.observe(s));
})();
