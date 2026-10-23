import { overlayRecipe, dialogRecipe, dialogTitleRecipe, dialogDescriptionRecipe, actionsRecipe } from "./recipes";
import * as AlertDialog from "@radix-ui/react-alert-dialog";
import type { ComponentProps, ReactNode } from "react";
import { Button } from "./button";

/**
 * Action that runs only after explicit confirmation. Focus starts on Cancel, stays inside the
 * dialog and returns to the trigger. The trigger keeps its DOM node when it is disabled.
 */
export function ConfirmAction({
  trigger,
  title,
  description,
  details,
  confirm,
  cancel,
  onConfirm,
  onCloseAutoFocus,
  disabled = false,
  variant = "danger",
}: {
  readonly trigger: string;
  readonly title: string;
  readonly description: string;
  /** Summary of the target and consequences shown between the description and the actions. */
  readonly details?: ReactNode | undefined;
  readonly confirm: string;
  readonly cancel: string;
  readonly onConfirm: () => void;
  /** Redirect focus to the next logical control when the confirmed action disables its trigger. */
  readonly onCloseAutoFocus?: ComponentProps<typeof AlertDialog.Content>["onCloseAutoFocus"];
  readonly disabled?: boolean | undefined;
  readonly variant?: "danger" | "primary" | undefined;
}) {
  return (
    <AlertDialog.Root>
      <AlertDialog.Trigger asChild>
        <Button variant={variant} disabled={disabled}>
          {trigger}
        </Button>
      </AlertDialog.Trigger>
      <AlertDialog.Portal>
        <AlertDialog.Overlay className={overlayRecipe()} />
        <AlertDialog.Content className={dialogRecipe()} onCloseAutoFocus={onCloseAutoFocus}>
          <AlertDialog.Title className={dialogTitleRecipe()}>{title}</AlertDialog.Title>
          <AlertDialog.Description className={dialogDescriptionRecipe()}>{description}</AlertDialog.Description>
          {details && <div className="mt-4">{details}</div>}
          <div className={actionsRecipe()}>
            <AlertDialog.Cancel asChild>
              <Button variant="secondary">{cancel}</Button>
            </AlertDialog.Cancel>
            <AlertDialog.Action asChild>
              <Button variant={variant} onClick={onConfirm}>
                {confirm}
              </Button>
            </AlertDialog.Action>
          </div>
        </AlertDialog.Content>
      </AlertDialog.Portal>
    </AlertDialog.Root>
  );
}
