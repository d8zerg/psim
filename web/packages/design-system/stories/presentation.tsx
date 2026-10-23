import { createContext, useContext, useLayoutEffect, useState, type ReactNode } from "react";
import { SelectField } from "../src";
import { presentation, type Density, type Mode, type Theme } from "../src/gen/tokens";
import { pseudolocalize } from "../src/pseudolocalize";
interface Settings {
  readonly theme: Theme;
  readonly density: Density;
  readonly mode: Mode;
  readonly language: "ru" | "en";
  readonly pseudo: boolean;
}
const Context = createContext<Settings>({
  theme: "dark",
  density: "comfortable",
  mode: "normal",
  language: "en",
  pseudo: false,
});
export function useStoryText() {
  const { language, pseudo } = useContext(Context);
  return (en: string, ru: string) => {
    const text = language === "ru" ? ru : en;
    return pseudo ? pseudolocalize(text) : text;
  };
}
function initialSettings(): Settings {
  const query = new URLSearchParams(location.search);
  const theme = query.get("psim-theme");
  return {
    theme: theme && Object.hasOwn(presentation.themes, theme) ? (theme as Theme) : "dark",
    density: presentation.densities.find((value) => value === query.get("psim-density")) ?? "comfortable",
    mode: presentation.modes.find((value) => value === query.get("psim-mode")) ?? "normal",
    language: query.get("psim-language") === "ru" ? "ru" : "en",
    pseudo: query.get("psim-pseudo") === "true",
  };
}
export function CataloguePresentation({ children }: { readonly children: ReactNode }) {
  const [settings, setSettings] = useState(initialSettings);
  useLayoutEffect(() => {
    const html = document.documentElement;
    html.dataset.theme = settings.theme;
    html.dataset.density = settings.density;
    html.lang = settings.language;
    html.style.colorScheme = presentation.themes[settings.theme].colorScheme;
    html.style.backgroundColor = presentation.themes[settings.theme].background;
  }, [settings]);
  return (
    <Context value={settings}>
      <div
        className="catalogue"
        id="catalogue"
        data-mode={settings.mode}
        role="region"
        aria-labelledby="catalogue-title"
      >
        <h1 id="catalogue-title">{settings.language === "ru" ? "Каталог компонентов" : "Component catalogue"}</h1>
        <div className="catalogue-settings">
          <SelectField
            label="Theme"
            value={settings.theme}
            onChange={(event) => {
              const theme = event.target.value;
              if (Object.hasOwn(presentation.themes, theme)) setSettings({ ...settings, theme: theme as Theme });
            }}
          >
            {Object.keys(presentation.themes).map((value) => (
              <option key={value}>{value}</option>
            ))}
          </SelectField>
          <SelectField
            label="Density"
            value={settings.density}
            onChange={(event) => {
              const density = presentation.densities.find((value) => value === event.target.value);
              if (density) setSettings({ ...settings, density });
            }}
          >
            {presentation.densities.map((value) => (
              <option key={value}>{value}</option>
            ))}
          </SelectField>
          <SelectField
            label="Mode"
            value={settings.mode}
            onChange={(event) => {
              const mode = presentation.modes.find((value) => value === event.target.value);
              if (mode) setSettings({ ...settings, mode });
            }}
          >
            {presentation.modes.map((value) => (
              <option key={value}>{value}</option>
            ))}
          </SelectField>
          <SelectField
            label="Language"
            value={settings.language}
            onChange={(event) => {
              setSettings({ ...settings, language: event.target.value === "ru" ? "ru" : "en" });
            }}
          >
            <option value="en">English</option>
            <option value="ru">Русский</option>
          </SelectField>
          <SelectField
            label="Pseudo"
            value={String(settings.pseudo)}
            onChange={(event) => {
              setSettings({ ...settings, pseudo: event.target.value === "true" });
            }}
          >
            <option value="false">Off</option>
            <option value="true">On</option>
          </SelectField>
        </div>
        <div className="catalogue-stack" id="catalogue-content">
          {children}
        </div>
      </div>
    </Context>
  );
}
export function Example({ name, children }: { readonly name: string; readonly children: ReactNode }) {
  return (
    <section className="catalogue-component" data-component={name} aria-label={name}>
      <h2>{name}</h2>
      {children}
    </section>
  );
}
