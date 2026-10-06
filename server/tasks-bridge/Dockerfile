FROM python:3.13-slim

COPY requirements.txt /app/requirements.txt
RUN pip install --no-cache-dir -r /app/requirements.txt

ENV PYTHONUNBUFFERED=1
WORKDIR /app
COPY bridge /app/bridge

# Holds Google refresh tokens and faces the internet, so not root. uid 10005:
# 10001-10004 are the other services that may share the box.
RUN useradd --uid 10005 --no-create-home --shell /usr/sbin/nologin tasksbridge \
 && mkdir -p /data && chown -R 10005:10005 /data /app
USER 10005:10005

EXPOSE 8080
# One worker: the store's lock is a threading.Lock, which is not a lock across
# processes.
CMD ["uvicorn", "bridge.app:app", "--host", "0.0.0.0", "--port", "8080", "--proxy-headers", "--forwarded-allow-ips", "*"]
