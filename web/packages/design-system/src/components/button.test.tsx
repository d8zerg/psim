import { cleanup, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";

import { cn } from "../cn";
import { Button } from "./button";

afterEach(cleanup);

describe("Button", () => {
  it("is a non-submitting button with the default variant", () => {
    render(<Button>Accept</Button>);
    const button = screen.getByRole("button", { name: "Accept" });
    expect(button.getAttribute("type")).toBe("button");
    expect(button.className).toContain("bg-primary");
  });

  it("applies variant and size and lets the caller override classes", () => {
    render(
      <Button variant="danger" size="lg" className="px-2" type="submit">
        Escalate
      </Button>,
    );
    const button = screen.getByRole("button", { name: "Escalate" });
    expect(button.getAttribute("type")).toBe("submit");
    expect(button.className).toContain("bg-danger");
    expect(button.className).toContain("px-2");
    expect(button.className).not.toContain("px-6");
  });

  it("renders the child element with button styles", () => {
    render(
      <Button asChild variant="ghost">
        <a href="/incidents">Incidents</a>
      </Button>,
    );
    const link = screen.getByRole("link", { name: "Incidents" });
    expect(link.className).toContain("hover:bg-muted");
  });
});

describe("cn", () => {
  it("drops falsy values and resolves Tailwind conflicts", () => {
    expect(cn("p-2", false, undefined, "p-4")).toBe("p-4");
  });
});
