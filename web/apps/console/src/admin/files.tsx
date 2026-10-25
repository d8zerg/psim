import { useEffect, useRef, useState } from "react";
import { useQuery } from "@tanstack/react-query";
import { Button, Field, Notice } from "@psim/design-system";
import { status, write, type AdminApi } from "./api";
import { Result, useWrites, WriteNotice } from "./actions";
import { list, record, string } from "./model";
import type { Permissions } from "./editor";
import { ConfirmAction } from "@psim/design-system/confirm-action";
import { useAdminStrings } from "./strings";
export function validFile(file: File, plan = false): boolean {
  return (
    file.size > 0 &&
    file.size <= (plan ? 20 : 64) * 1024 * 1024 &&
    (plan ? ["image/png", "image/jpeg"].includes(file.type) : /\.(?:csv|json)$/iu.test(file.name))
  );
}
export function Imports({ api, permissions }: { readonly api: AdminApi; readonly permissions: Permissions }) {
  const { t } = useAdminStrings();
  const [file, setFile] = useState<File>();
  const [error, setError] = useState(false);
  const [checkedFile, setCheckedFile] = useState<File>();
  const [job, setJob] = useState("");
  const [checking, setChecking] = useState(false);
  const actions = useWrites(api, permissions.denied);
  const result = useQuery({
    queryKey: ["admin", "import", job],
    queryFn: ({ signal }) => api.get(`/v1/imports/${encodeURIComponent(job)}`, signal),
    retry: false,
    enabled: !!job,
    refetchInterval: (query) => (record(query.state.data?.importJob).state === "IMPORT_STATE_RUNNING" ? 1000 : false),
  });
  const completed = record(result.data?.importJob);
  const passed = checking && completed.state === "IMPORT_STATE_COMPLETED" && Number(completed.rowsFailed ?? 0) === 0;
  useEffect(() => {
    if ([401, 403].includes(status(result.error))) permissions.denied();
  }, [result.error, permissions]);
  const ready = permissions.writable && !actions.pending && !actions.uncertain;
  const send = (dryRun: boolean) => {
    if (!file) return;
    const selected = file;
    const format = /\.csv$/iu.test(file.name) ? "csv" : "json";
    void actions.perform(
      write(
        `/v1/imports?format=${format}&dryRun=${String(dryRun)}`,
        file,
        "POST",
        undefined,
        format === "csv" ? "text/csv" : "application/json",
      ),
      (response) => {
        setJob(string(record(response.importJob).importId));
        setChecking(dryRun);
        setCheckedFile(dryRun ? selected : undefined);
        actions.setResult(undefined);
      },
    );
  };
  return (
    <section className="admin-fields">
      <h2>{t.imports}</h2>
      <p>{t.importHelp}</p>
      <WriteNotice actions={actions} writable={permissions.writable} />
      <Field
        label={t.file}
        type="file"
        accept=".csv,.json"
        disabled={!ready}
        onChange={(event) => {
          const selected = event.target.files?.[0];
          setError(!!selected && !validFile(selected));
          setFile(selected && validFile(selected) ? selected : undefined);
          setCheckedFile(undefined);
          setJob("");
          setChecking(false);
        }}
      />
      {error && <Notice tone="warning">{t.invalidFile}</Notice>}
      <Button
        disabled={!ready || !file}
        onClick={() => {
          send(true);
        }}
      >
        {t.checkImport}
      </Button>
      <ConfirmAction
        trigger={t.applyImport}
        title={t.applyImport}
        description={t.confirmHint}
        details={<p>{file?.name}</p>}
        confirm={t.confirm}
        cancel={t.cancel}
        disabled={!ready || !passed || checkedFile !== file}
        onConfirm={() => {
          send(false);
        }}
      />
      {result.data && !result.error && <Result value={result.data} />}
    </section>
  );
}
export function FloorPlans({ api, permissions }: { readonly api: AdminApi; readonly permissions: Permissions }) {
  const { t } = useAdminStrings();
  const [input, setInput] = useState("");
  const [id, setId] = useState("");
  const [file, setFile] = useState<File>();
  const [width, setWidth] = useState("100");
  const [height, setHeight] = useState("100");
  const [error, setError] = useState(false);
  const [busy, setBusy] = useState(false);
  const [page, setPage] = useState("");
  const [preview, setPreview] = useState("");
  const imageElement = useRef<HTMLImageElement>(null);
  const actions = useWrites(api, permissions.denied);
  const floor = useQuery({
    queryKey: ["admin", "floor", id],
    queryFn: ({ signal }) => api.get(`/v1/locations/${encodeURIComponent(id)}`, signal),
    enabled: !!id,
    retry: false,
  });
  const versions = useQuery({
    queryKey: ["admin", "floor-versions", id, page],
    queryFn: ({ signal }) =>
      api.get(
        `/v1/locations/${encodeURIComponent(id)}/floor-plan/versions?${new URLSearchParams({ pageSize: "100", pageToken: page })}`,
        signal,
      ),
    enabled: !!id && floor.isSuccess && !floor.error,
    retry: false,
  });
  const image = useQuery({
    queryKey: ["admin", "floor-image", id, preview],
    queryFn: ({ signal }) =>
      api.blob(
        `/v1/locations/${encodeURIComponent(id)}/floor-plan?${new URLSearchParams({ version: preview })}`,
        signal,
      ),
    enabled: !!id && !!preview && !floor.error && !versions.error,
    retry: false,
  });
  useEffect(() => {
    if ([floor.error, versions.error, image.error].some((error) => [401, 403].includes(status(error))))
      permissions.denied();
  }, [floor.error, versions.error, image.error, permissions]);
  useEffect(() => {
    if (!image.data || image.error || !["image/png", "image/jpeg"].includes(image.data.type)) return;
    const next = URL.createObjectURL(image.data);
    if (imageElement.current) imageElement.current.src = next;
    return () => {
      URL.revokeObjectURL(next);
    };
  }, [image.data, image.error]);
  const location = !floor.error ? record(floor.data?.location) : {};
  const ready = permissions.writable && !actions.pending && !actions.uncertain && !busy;
  const upload = async () => {
    if (!file) return;
    setBusy(true);
    setError(false);
    try {
      const bitmap = await createImageBitmap(file);
      const pixels = bitmap.width * bitmap.height;
      bitmap.close();
      if (pixels > 16000000) {
        setError(true);
        return;
      }
      await actions.perform(
        write(
          `/v1/locations/${encodeURIComponent(id)}/floor-plan?${new URLSearchParams({ width, height })}`,
          file,
          "PUT",
          string(location.version),
          file.type,
        ),
        (result) => {
          actions.setResult({ status: t.saved, floorPlan: result.floorPlan ?? result.file ?? {} });
          void floor.refetch();
          void versions.refetch();
        },
      );
    } catch {
      setError(true);
    } finally {
      setBusy(false);
    }
  };
  return (
    <section className="admin-fields">
      <h2>{t.floorPlans}</h2>
      <WriteNotice actions={actions} writable={permissions.writable} />
      <form
        onSubmit={(event) => {
          event.preventDefault();
          setId(input);
          setPreview("");
          setPage("");
        }}
      >
        <Field
          label={t.floor}
          required
          value={input}
          disabled={!ready}
          onChange={(event) => {
            setInput(event.target.value);
          }}
        />
        <Button type="submit" disabled={!ready}>
          {t.select}
        </Button>
      </form>
      {floor.error && <Notice tone="warning">{t.failed}</Notice>}
      {location.kind === "LOCATION_KIND_FLOOR" && (
        <>
          <p>
            {string(location.name)} / {t.version}: {string(location.version)}
          </p>
          <Field
            label={t.width}
            type="number"
            min="0.01"
            step="any"
            value={width}
            disabled={!ready}
            onChange={(event) => {
              setWidth(event.target.value);
            }}
          />
          <Field
            label={t.height}
            type="number"
            min="0.01"
            step="any"
            value={height}
            disabled={!ready}
            onChange={(event) => {
              setHeight(event.target.value);
            }}
          />
          <Field
            label={t.file}
            type="file"
            accept="image/png,image/jpeg"
            disabled={!ready}
            onChange={(event) => {
              const selected = event.target.files?.[0];
              setError(!!selected && !validFile(selected, true));
              setFile(selected && validFile(selected, true) ? selected : undefined);
            }}
          />
          {error && <Notice tone="warning">{t.invalidFile}</Notice>}
          <Button
            disabled={!ready || !file || !(Number(width) > 0 && Number(height) > 0)}
            onClick={() => {
              void upload();
            }}
          >
            {t.upload}
          </Button>
          {!versions.error &&
            list(versions.data?.files)
              .map(record)
              .map((entry) => (
                <div key={string(entry.version)}>
                  <span>
                    {t.version}: {string(entry.version)} / {string(entry.sha256)}
                  </span>
                  <Button
                    onClick={() => {
                      setPreview(string(entry.version));
                    }}
                  >
                    {t.download} {string(entry.version)}
                  </Button>
                </div>
              ))}
          {string(versions.data?.nextPageToken) && (
            <Button
              onClick={() => {
                setPage(string(versions.data?.nextPageToken));
              }}
            >
              {t.next}
            </Button>
          )}
          {page && (
            <Button
              onClick={() => {
                setPage("");
              }}
            >
              {t.previous}
            </Button>
          )}
          {image.data && !image.error && (
            <div className="admin-preview">
              <img ref={imageElement} alt={`${t.floorPlans}: ${preview}`} />
              <Button
                onClick={() => {
                  setPreview("");
                }}
              >
                {t.close}
              </Button>
            </div>
          )}
        </>
      )}
    </section>
  );
}
