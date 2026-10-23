import { useState } from "react";
import { Badge, Button, Field, TableContainer } from "../src";
import { DataTable } from "../src/components/collections";
const initial = Array.from({ length: 5000 }, (_, index) => ({
  id: `ROW-${String(index).padStart(5, "0")}`,
  source: `SENSOR-${String(index % 20)}`,
  priority: index % 5 ? "P3" : "P1",
}));
/** Preserves the original non-virtual row mapping for an identical deterministic stress fixture. */
export function CollectionBenchmark() {
  const baseline = new URLSearchParams(location.search).get("renderer") === "baseline";
  const [records, setRecords] = useState(initial);
  const [query, setQuery] = useState("");
  const [revision, setRevision] = useState(0);
  return (
    <div data-testid="benchmark" data-revision={revision}>
      <Button
        onClick={() => {
          setRevision((previous) => previous + 1);
          setRecords((previous) =>
            previous.map((record, index) =>
              index % 10 === 0 ? { ...record, source: `SENSOR-${String(index % 20)}-${String(revision + 1)}` } : record,
            ),
          );
        }}
      >
        Update batch
      </Button>
      {baseline ? (
        <>
          <Field
            label="Search benchmark"
            value={query}
            onChange={(event) => {
              setQuery(event.target.value);
            }}
          />
          <TableContainer label="Benchmark records">
            <table className="ds-table">
              <thead>
                <tr>
                  <th scope="col">ID</th>
                  <th scope="col">Source</th>
                  <th scope="col">Priority</th>
                </tr>
              </thead>
              <tbody>
                {records
                  .filter((record) =>
                    `${record.id} ${record.source} ${record.priority}`
                      .toLocaleLowerCase()
                      .includes(query.toLocaleLowerCase()),
                  )
                  .map((record) => (
                    <tr key={record.id} data-id={record.id}>
                      <td>{record.id}</td>
                      <td>{record.source}</td>
                      <td>
                        <Badge>{record.priority}</Badge>
                      </td>
                    </tr>
                  ))}
              </tbody>
            </table>
          </TableContainer>
        </>
      ) : (
        <DataTable
          label="Benchmark records"
          data={records}
          columns={[
            { accessorKey: "id", header: "ID" },
            { accessorKey: "source", header: "Source" },
            {
              accessorKey: "priority",
              header: "Priority",
              cell: (info) => <Badge>{info.row.original.priority}</Badge>,
            },
          ]}
          labels={{
            search: "Search benchmark",
            select: "Select",
            columns: "Columns",
            pin: "Pin",
            bulk: "Apply",
            empty: "Empty",
          }}
        />
      )}
    </div>
  );
}
