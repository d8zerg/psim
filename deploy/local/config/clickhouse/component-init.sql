-- Development identities only; deployment uses operator-managed secrets and TLS.
CREATE USER IF NOT EXISTS psim_ch_loader IDENTIFIED WITH sha256_password BY 'psim-dev-ch-loader';
GRANT SELECT, INSERT ON default.* TO psim_ch_loader;
CREATE USER IF NOT EXISTS psim_ch_reader IDENTIFIED WITH sha256_password BY 'psim-dev-ch-reader';
GRANT SELECT ON default.* TO psim_ch_reader;
