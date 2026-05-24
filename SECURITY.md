# Security Policy

## Reporting a vulnerability

Please report security vulnerabilities **privately**. Do not open a public issue
for a security problem.

- Preferred: open a private advisory via GitHub Security Advisories
  (the repository's "Security" tab → "Report a vulnerability").
- Alternatively, email the maintainer at <frederic.aime@gmail.com> with a
  description, reproduction steps, and impact.

You can expect an initial acknowledgement within a few business days. Once a fix
is available we will coordinate a disclosure timeline with you.

## Scope notes

This bridge can invoke arbitrary D-Bus methods on the session and system buses.
When deploying it for an autonomous agent, run it with the least privilege
necessary and treat its reachable D-Bus surface as part of your threat model.

## Supported versions

The project is pre-1.0; only the latest released version receives security fixes.
