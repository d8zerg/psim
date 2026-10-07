import { RouterProvider } from "@tanstack/react-router";
import { cleanup, render, screen } from "@testing-library/react";
import { afterEach, describe, expect, it } from "vitest";

import { createAppRouter } from "./router";

afterEach(cleanup);

describe("console shell", () => {
  it("renders the home page with the connection state", async () => {
    render(<RouterProvider router={createAppRouter()} />);
    expect(await screen.findByRole("heading", { name: "PSIM Platform" })).toBeDefined();
    expect(screen.getByTestId("connection-status").textContent).toBe("offline");
  });
});
