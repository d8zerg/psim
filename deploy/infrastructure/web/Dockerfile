ARG BASE
FROM ${BASE}
COPY --chown=60024:60024 server.mjs /srv/server.mjs
COPY --chown=60024:60024 public /srv/public
USER 60024:60024
ENTRYPOINT ["node", "/srv/server.mjs"]
