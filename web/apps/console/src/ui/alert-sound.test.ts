import { afterEach, expect, it, vi } from "vitest";
import { playAlert } from "./alert-sound";
afterEach(() => vi.unstubAllGlobals());
it("plays a bounded tone and releases the audio context", async () => {
  const close = vi.fn();
  class Audio {
    currentTime = 0;
    destination = {};
    resume = vi.fn();
    close = close;
    createGain() {
      return {
        gain: { setValueAtTime: vi.fn(), linearRampToValueAtTime: vi.fn(), exponentialRampToValueAtTime: vi.fn() },
        connect: vi.fn(),
      };
    }
    createOscillator() {
      const oscillator = {
        type: "sine",
        frequency: { value: 0 },
        connect: vi.fn(),
        start: vi.fn(),
        onended: () => undefined,
        stop: () => {
          queueMicrotask(() => {
            oscillator.onended();
          });
        },
      };
      return oscillator;
    }
  }
  vi.stubGlobal("AudioContext", Audio);
  await playAlert();
  expect(close).toHaveBeenCalledOnce();
  vi.spyOn(Audio.prototype, "createGain").mockImplementation(() => {
    throw new Error("unavailable");
  });
  await expect(playAlert()).rejects.toThrow("unavailable");
  expect(close).toHaveBeenCalledTimes(2);
});
