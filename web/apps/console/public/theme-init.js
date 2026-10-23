// Generated early presentation.
(() => {
  const choices = {"themes":{"dark":{"background":"#10151e","colorScheme":"dark"},"light":{"background":"#f2f5f9","colorScheme":"light"},"contrast":{"background":"#000000","colorScheme":"dark"},"night":{"background":"#0e0b09","colorScheme":"dark"}},"densities":["comfortable","compact","wall"],"modes":["normal","crisis","training","wall"]};
  let saved = {};
  try { saved = JSON.parse(localStorage.getItem("psim.ui") ?? "{}"); } catch { /* Use defaults. */ }
  if (saved === null || typeof saved !== "object") saved = {};
  const theme = typeof saved.theme === "string" && Object.hasOwn(choices.themes, saved.theme) ? saved.theme : "dark";
  const root = document.documentElement;
  root.dataset.theme = theme;
  root.dataset.density = choices.densities.includes(saved.density) ? saved.density : "comfortable";
  root.dataset.mode = choices.modes.includes(saved.mode) ? saved.mode : "normal";
  root.lang = saved.language === "en" ? "en" : "ru";
  Object.assign(root.style, { backgroundColor: choices.themes[theme].background, colorScheme: choices.themes[theme].colorScheme });
})();
