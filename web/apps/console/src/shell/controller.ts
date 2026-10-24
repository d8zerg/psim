import type { SessionAuth, SessionState } from "../auth";
import type { FeedController, FeedState } from "../feed/controller";
import { allowedModules } from "./modules";
export const emptyFeed: FeedState = { rows: [], status: "offline", error: "", loading: false, loaded: false };
export interface ShellState {
  readonly session: SessionState;
  readonly feed: FeedState;
}
export type AuthSession = Pick<SessionAuth, "getSnapshot" | "subscribe" | "token" | "login" | "logout" | "invalidate">;
export class ShellController {
  private state: ShellState;
  private running = false;
  private epoch = 0;
  private context: string | undefined;
  private feedEpoch = 0;
  private feedPending = false;
  private feed: FeedController | undefined;
  private releaseFeed: (() => void) | undefined;
  private releaseAuth: (() => void) | undefined;
  private readonly listeners = new Set<() => void>();
  constructor(
    readonly auth: AuthSession,
    private readonly makeFeed: () => FeedController | Promise<FeedController>,
  ) {
    this.state = { session: auth.getSnapshot(), feed: emptyFeed };
  }
  getSnapshot = (): ShellState => this.state;
  subscribe = (listener: () => void): (() => void) => {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  };
  private update(state: ShellState): void {
    this.state = state;
    this.listeners.forEach((listener) => {
      listener();
    });
  }
  private clearFeed(): void {
    this.feedEpoch += 1;
    this.feedPending = false;
    this.releaseFeed?.();
    this.releaseFeed = undefined;
    this.feed?.stop();
    this.feed = undefined;
  }
  private sync = (): void => {
    if (!this.running) return;
    const session = this.auth.getSnapshot();
    const context = session.profile?.context;
    if (context !== this.context || (!this.feed && !this.feedPending && context !== undefined)) {
      this.context = context;
      this.clearFeed();
      const canReadFeed = allowedModules(session.profile).some((module) => module.key === "console");
      this.update({ session, feed: canReadFeed ? { ...emptyFeed, loading: true, status: "connecting" } : emptyFeed });
      if (canReadFeed) {
        const feedEpoch = this.feedEpoch;
        this.feedPending = true;
        void Promise.resolve()
          .then(this.makeFeed)
          .then((feed) => {
            if (!this.running || feedEpoch !== this.feedEpoch) {
              feed.stop();
              return;
            }
            this.feedPending = false;
            this.feed = feed;
            this.releaseFeed = feed.subscribe(() => {
              if (this.feed === feed && this.running) this.update({ ...this.state, feed: feed.getSnapshot() });
            });
            return feed.start().catch((error: unknown) => {
              if (this.feed === feed) feed.fail(error);
            });
          })
          .catch(() => {
            if (this.running && feedEpoch === this.feedEpoch) {
              this.feedPending = false;
              this.update({ ...this.state, feed: { ...emptyFeed, error: "Module unavailable" } });
            }
          });
      }
    } else this.update({ ...this.state, session });
  };
  start(): void {
    if (this.running) return;
    this.running = true;
    const epoch = ++this.epoch;
    this.releaseAuth = this.auth.subscribe(this.sync);
    this.sync();
    void this.auth
      .token()
      .then(this.sync)
      .catch(() => {
        if (this.running && this.epoch === epoch) this.sync();
      });
  }
  stop(): void {
    this.running = false;
    this.epoch += 1;
    this.releaseAuth?.();
    this.releaseAuth = undefined;
    this.clearFeed();
    this.context = undefined;
    this.state = { session: this.auth.getSnapshot(), feed: emptyFeed };
  }
  reconnect = (): void => {
    if (this.feed) this.feed.retry();
    else this.sync();
  };
}
