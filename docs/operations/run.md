
# Running StratoWeave
A StratoWeave-based Orchestrator is a single binary that can be deployed on any
Linux-based system.

## Deployment
The simplest way to deploy a StratoWeave orchestrator is wrap up the binary
into a container image and deploy it to a container orchestration platform such as
Kubernetes. The following example shows a simple Dockerfile that wraps the
SORESPO binary into a container image.

```dockerfile title="Dockerfile for a StratoWeave orchestrator"
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y libssh-4
COPY out/bin/sorespo /usr/local/bin/sorespo
ENTRYPOINT ["/usr/local/bin/sorespo"]
```

!!! tip
    You could use a more minimal base image such as `alpine` or `debian:slim`,
    and in the future we intend to enable you to use a `FROM scratch` base
    image.

## Logging
StratoWeave orchestrators log to `stdout` and `stderr` by default. When you run
the orchestrator in a container, these streams are captured by the container
runtime and can be viewed with `docker logs` or `kubectl logs`.
Through [Docker logging drivers](https://docs.docker.com/engine/logging/configure/),
you can also redirect these streams to a file or a logging service of your
choice.

## Run-time configuration
Running `--help` is the fastest way to confirm the exact interface exposed by
the StratoWeave orchestrator binary you are using. The following example shows
the output of `--help` for SORESPO, the reference implementation of a
StratoWeave orchestrator.

```console title="Inspect runtime options for a StratoWeave orchestrator"
out/bin/sorespo --help
Usage: sorespo [OPTIONS] [--] [FILE ...]

Arguments:
  FILE ...
      Startup configuration files, applied in order
      env: STRATOWEAVE_STARTUP_CONFIG_FILES; default: []

Options:
  --db TEXT
      TTT database: an LMDB path or a database URL
      env: STRATOWEAVE_DB; optional
  --exit-on-done / --no-exit-on-done
      Exit after startup config has been applied
      env: STRATOWEAVE_EXIT_ON_DONE; default: False
  --http.port INT
      HTTP listen port
      env: STRATOWEAVE_HTTP__PORT; default: 80
  --netconf.port INT
      NETCONF SSH listen port
      env: STRATOWEAVE_NETCONF__PORT; default: 830
  --netconf.user TEXT
      NETCONF SSH username (v1 static credential)
      env: STRATOWEAVE_NETCONF__USER; default: admin
  ...
```

Every setting has the same three spellings: a key in an AON settings file, an
environment variable, and a command-line option. They resolve in that order,
so the command line wins. Settings that belong to one subsystem share a path,
which is a nesting in the settings file and a `__` separator in the
environment.

```aon title="system.aon"
db = "/var/lib/sorespo"
http:
    port = 8080
netconf:
    port = 1830
    user = "netops"
```

The `FILE` arguments are startup configuration documents, applied in the order
given. Operators often keep a base system configuration in one file and add
environment-specific overlays as later arguments.

The most important settings are:

- `--db` enables persistence, taking either an LMDB directory or a database
  URL. Without it the orchestrator runs stateless and all configuration is
  lost on shutdown.
- `--http.port` changes the HTTP listener, which defaults to `80`.
- `--netconf.port` changes the NETCONF over SSH listener, which defaults to
  `830`.
- `--netconf.ssh.host-key` points to a persistent SSH host key. If you omit
  it, an ephemeral in-memory key is generated at startup.
- `--netconf.user` and `--netconf.password` configure the built-in static
  NETCONF credentials. They default to `admin` / `admin` and should be
  overridden outside local testing.
- `--exit-on-done` applies the supplied startup configuration and then exits
  instead of continuing to serve traffic. This is useful for CI validation or
  one-shot initialization jobs.

### Browser authentication

HTTP authentication is enabled when an OIDC issuer or a local HTTP account is
configured. Both methods can be enabled together; the local account remains
available when the identity provider is unavailable. Incomplete authentication
settings fail at startup. With neither method configured, the existing
unauthenticated HTTP behavior remains available.

```aon title="system.aon"
http:
    port = 8443
    tls:
        certificate_file = "/run/secrets/https-certificate.pem"
        private_key_file = "/run/secrets/https-private-key.pem"
    auth:
        public_origin = "https://sorespo.example.org:8443"
        session_idle_seconds = 1800
        session_absolute_seconds = 28800
        local:
            username = "operator"
            password_hash_file = "/run/secrets/http-password.bcrypt"
        oidc:
            issuer = "https://identity.example.org/dex"
            client_id = "sorespo"
            client_secret_file = "/run/secrets/oidc-client-secret"
            allowed_subjects = ["provider-issued-subject-id"]
            scopes = ["openid", "profile", "email", "offline_access"]
```

Register the application as a confidential OIDC client with the exact redirect
URI `https://sorespo.example.org:8443/auth/oidc/callback`. The provider must
support Authorization Code flow, S256 PKCE, discovery, JWKS, and RS256 ID
tokens. Add the provider's stable `sub` identifiers to `allowed_subjects`;
email addresses and display names do not grant access. Every admitted identity
has full access to the current APIs, including configuration and device
credentials. Group mappings and restricted roles are not implemented.

The local account is separate from NETCONF's username and password. Its file
contains a bcrypt `$2b$` or `$2y$` hash, not a plaintext password. Supported
bcrypt costs are 4 through 16; use an appropriate production cost when
generating the hash. Passwords must fit bcrypt's 72-byte input limit. Protect
the password hash, client secret and TLS private key files using deployment
secret mounts and file permissions. File contents are loaded at startup.

`public_origin` is the exact browser-visible scheme, host and optional port,
without a trailing slash. A TLS-terminating reverse proxy can serve that
origin instead of native TLS; omit both `http.tls` files in that deployment
and restrict access to the backend listener. The backend does not infer a
trusted origin from forwarded headers. Provider HTTPS connections use normal
certificate verification and the runtime trust store. The explicit
`http.auth.allow_insecure_http = True` option permits development HTTP only
for `localhost`, `.localhost` names and loopback addresses.

The SPA and APIs share one origin. The browser receives an opaque HttpOnly
cookie, while access, refresh and ID tokens remain in the backend. A login
redirect carries a one-use authorization code; the callback immediately
redirects to a clean application URL. The SPA fetches `/auth/session` to learn
the available login methods, identity and CSRF token. Local login, logout and
API mutations require that CSRF token in `X-CSRF-Token` and an `Origin` matching
`public_origin`. Device resynchronization uses `POST /device/{name}/resync`.
Programmatic HTTP clients can use the same login and cookie flow; bearer-token
API authentication is not provided by this implementation.

Sessions are process-local, expire after the configured idle or absolute
lifetime, and are invalidated by a restart. Idle time is measured from API
activity, including SPA polling. Refresh is performed in the backend with one
refresh operation per session at a time. Logout invalidates the local session
and requests provider revocation when available; it does not sign the user out
of the provider's other applications. An expired or revoked session returns
`401`; a temporary provider failure denies API access while preserving local
login availability. Login attempts and session storage are bounded.

Authentication is rejected in Acton durable-runtime builds because their
actor snapshots could persist credentials. The separate StratoWeave `--db`
option for TTT data remains supported and does not persist browser sessions.
HTTP wire-level tracing is disabled for the standard authenticated runtime;
router access logs contain paths and status codes without token-bearing query
strings, cookies or request bodies.

For example, the environment spelling of `http.auth.public_origin` is
`STRATOWEAVE_HTTP__AUTH__PUBLIC_ORIGIN`. Lists use AON syntax in environment
variables, such as `STRATOWEAVE_HTTP__AUTH__OIDC__ALLOWED_SUBJECTS='["subject"]'`.

### NETCONF server SSH transport

The SSH transport the northbound NETCONF server offers is configured under
`netconf.ssh`. Leave it alone and the SSH library's defaults apply.

```aon title="system.aon"
netconf:
    ssh:
        cipher = ["aes256-gcm@openssh.com", "aes256-ctr"]
        host_key_algorithm = ["ssh-ed25519"]
        rekey_after_seconds = 3600
```

The same settings are `--netconf.ssh.cipher` on the command line, repeated once
per value, and `STRATOWEAVE_NETCONF__SSH__CIPHER` in the environment.

The algorithm lists (`cipher`, `mac`, `key_exchange`, `host_key_algorithm`,
`public_key_algorithm`, `compression_algorithm`) are ordered preference lists,
most preferred first, and an empty one means the library default. The rest are
`compression_level` (1-9, default 7), `rekey_after_bytes`,
`rekey_after_seconds` and `minimum_rsa_bits`. Both rekey limits are optional:
unset means no byte limit beyond the negotiated cipher's own, and no
time-based rekeying.

An algorithm name the SSH library does not support fails at startup rather
than on the first connection. The equivalent settings for connections *to*
devices live in the device's `ssh` container, described in
[Device management](devices.md).

### Acton Runtime System
As for any Acton application, you can also configure the [Acton Runtime System](https://acton.guide/rts.html).

!!! note
    There is generally no need to change these settings, the defaults are
    suitable for most deployments.
