# Peer run logs

Each run stores one CSV file per peer in `logs/runs/<run_id>/peer_<port>.csv`.
The tracker output for that run is `tracker.log`.

The columns are: `timestamp`, `elapsed_seconds`, `peer_port`, `event`,
`remote_ip`, `remote_port`, `piece_index`, `bytes`, `status`, and `details`.
`peer_port` is the local peer writing the row; `remote_port` identifies the
other peer. A received/verified piece has the source peer in `remote_port`; an
uploaded piece has the recipient there. `piece_index` and `bytes` identify the
whole piece involved.

Events: `peer_connected`, `peer_disconnected`, `piece_requested`,
`piece_received`, `piece_verified`, `piece_hash_failed`, `piece_uploaded`, and
`download_complete`. The final event's elapsed time is in `elapsed_seconds`;
its `details` includes the verified-piece count.

Open a CSV in Excel with **Data → From Text/CSV**. To see where each piece came
from, create a PivotTable filtered to `piece_verified`, using `piece_index` as
Rows and `remote_port` as Columns. For upload contribution, filter to
`piece_uploaded`, place `peer_port` in Rows and sum `bytes`, then insert a bar
chart.
