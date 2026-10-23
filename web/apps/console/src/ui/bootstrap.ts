import { loadStrings } from "./locales/loader";
import { usePreferences } from "./preferences";
/** A locale failure offers reload before mounting authentication or live data. */
export async function startWithLocale(root: HTMLElement, render: () => void, design = false): Promise<void> {
  try {
    await loadStrings(usePreferences.getState().language, design);
    render();
  } catch {
    const message = document.createElement("p");
    message.setAttribute("role", "alert");
    message.textContent = "Не удалось загрузить интерфейс / Could not load the interface";
    const retry = document.createElement("button");
    retry.type = "button";
    retry.textContent = "Повторить / Retry";
    retry.onclick = () => {
      location.reload();
    };
    root.replaceChildren(message, retry);
  }
}
