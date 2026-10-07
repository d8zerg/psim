// A delta is applied only when the aggregate version is newer than the known one (ADR-019, item 4):
// after a resume the server may repeat changes the snapshot already contains.
export class VersionGate {
  private readonly versions = new Map<string, bigint>();

  /** Record the version of an aggregate from a snapshot. */
  seed(aggregateId: string, version: bigint): void {
    const known = this.versions.get(aggregateId);
    if (known === undefined || version > known) {
      this.versions.set(aggregateId, version);
    }
  }

  /** True when the change is new and must be applied; the version is then remembered. */
  accept(aggregateId: string, version: bigint): boolean {
    const known = this.versions.get(aggregateId);
    if (known !== undefined && version <= known) {
      return false;
    }
    this.versions.set(aggregateId, version);
    return true;
  }

  forget(aggregateId: string): void {
    this.versions.delete(aggregateId);
  }
}
