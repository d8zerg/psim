import type { LucideIcon } from "lucide-react";
import { AlertTriangle, Bell, Camera, DoorOpen, Radio, ShieldCheck, Unplug, CircleHelp } from "lucide-react";

export const domainIcons = {
  alarm: Bell,
  warning: AlertTriangle,
  camera: Camera,
  door: DoorOpen,
  sensor: Radio,
  approval: ShieldCheck,
  disconnected: Unplug,
  provenance: CircleHelp,
} as const;
type IconMeaning =
  { readonly label: string; readonly decorative?: false } | { readonly decorative: true; readonly label?: never };
/** An icon is either named or explicitly decorative beside an existing text label. */
export function Icon({
  icon: Glyph,
  label,
  decorative,
  className,
}: IconMeaning & { readonly icon: LucideIcon; readonly className?: string }) {
  return (
    <Glyph
      className={className}
      size="1em"
      aria-hidden={decorative ? true : undefined}
      aria-label={decorative ? undefined : label}
      role={decorative ? undefined : "img"}
    />
  );
}
