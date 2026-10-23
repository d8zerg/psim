export async function playAlert({
  priority = "P2",
  volume = 0.3,
}: { readonly priority?: string; readonly volume?: number } = {}): Promise<void> {
  if (volume <= 0) return;
  const context = new AudioContext();
  try {
    await context.resume();
    const oscillator = context.createOscillator();
    const gain = context.createGain();
    oscillator.type = "sine";
    oscillator.frequency.value = priority === "P1" ? 880 : 660;
    gain.gain.setValueAtTime(0, context.currentTime);
    gain.gain.linearRampToValueAtTime(0.12 * Math.max(0, Math.min(1, volume)), context.currentTime + 0.015);
    gain.gain.exponentialRampToValueAtTime(0.001, context.currentTime + 0.18);
    oscillator.connect(gain);
    gain.connect(context.destination);
    await new Promise<void>((resolve) => {
      oscillator.onended = () => {
        resolve();
      };
      oscillator.start();
      oscillator.stop(context.currentTime + 0.2);
    });
  } finally {
    await context.close();
  }
}
