# Build from the repository root so the API record comes along:
#   docker build -f server/appstore/Dockerfile -t crossplay-appstore .
FROM python:3.12-slim
WORKDIR /srv
COPY server/appstore/crossplay_store ./crossplay_store
COPY docs/app-api/compat.json ./compat.json
RUN useradd --system --uid 10001 store && mkdir /data && chown store /data
USER store
ENV APPSTORE_DATA=/data APPSTORE_API=/srv/compat.json APPSTORE_PORT=8080
VOLUME /data
EXPOSE 8080
HEALTHCHECK CMD python3 -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8080/healthz')"
CMD ["python3", "-m", "crossplay_store.server"]
