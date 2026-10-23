import { SelectField } from "@psim/design-system";
import { useRef, useState } from "react";
import { loadStrings, type Locale } from "./locales/loader";
import { isDensity, isMode, isTheme, setPreferences, usePreferences } from "./preferences";
import { useStrings } from "./strings";
export function DisplaySettings({ design = false }: { readonly design?: boolean }) {
  const preferences = usePreferences();
  const t = useStrings();
  const request = useRef(0);
  const [pending, setPending] = useState(false);
  const [failed, setFailed] = useState(false);
  async function changeLanguage(language: Locale) {
    const current = ++request.current;
    setPending(true);
    setFailed(false);
    try {
      await loadStrings(language, design);
      if (request.current === current) setPreferences({ language });
    } catch {
      if (request.current === current) setFailed(true);
    } finally {
      if (request.current === current) setPending(false);
    }
  }
  return (
    <div className="display-settings" role="group" aria-label={t.settings}>
      <SelectField
        label={t.theme}
        value={preferences.theme}
        onChange={(event) => {
          setPreferences({ theme: isTheme(event.target.value) ? event.target.value : "dark" });
        }}
      >
        <option value="dark">{t.dark}</option>
        <option value="light">{t.light}</option>
        <option value="contrast">{t.contrast}</option>
        <option value="night">{t.night}</option>
      </SelectField>
      <SelectField
        label={t.density}
        value={preferences.density}
        onChange={(event) => {
          setPreferences({ density: isDensity(event.target.value) ? event.target.value : "comfortable" });
        }}
      >
        <option value="comfortable">{t.comfortable}</option>
        <option value="compact">{t.compact}</option>
        <option value="wall">{t.wall}</option>
      </SelectField>
      <SelectField
        label={t.mode}
        value={preferences.mode}
        onChange={(event) => {
          setPreferences({ mode: isMode(event.target.value) ? event.target.value : "normal" });
        }}
      >
        <option value="normal">{t.normalMode}</option>
        <option value="crisis">{t.crisis}</option>
        <option value="training">{t.training}</option>
        <option value="wall">{t.wall}</option>
      </SelectField>
      <SelectField
        label={t.language}
        value={preferences.language}
        aria-busy={pending}
        onChange={(event) => {
          void changeLanguage(event.target.value === "en" ? "en" : "ru");
        }}
      >
        <option value="ru">Русский</option>
        <option value="en">English</option>
      </SelectField>
      {failed && <p role="alert">{t.localeLoadError}</p>}
    </div>
  );
}
