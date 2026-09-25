// PulseAmp website: small enhancements (the page works fine without JavaScript)

// Tailor the download area to the visitor's system
(function showOsNote() {
  const platform = ((navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || "") +
                   " " + navigator.userAgent;
  const isWindows = /win/i.test(platform);
  const isLinux = /linux|x11/i.test(platform) && !/android/i.test(platform);
  if (isLinux) {
    const note = document.getElementById("os-note-linux");
    if (note) note.hidden = false;
    // Make the AppImage the highlighted download
    document.querySelectorAll(".downloads .btn").forEach((b) => b.classList.remove("primary"));
    const linux = document.getElementById("linux-download");
    if (linux) { linux.classList.add("primary"); linux.parentNode.prepend(linux); }
  } else if (!isWindows) {
    const note = document.getElementById("os-note-other");
    if (note) note.hidden = false;
  }
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
