# Security Policy

## Reporting a Vulnerability

If you discover a security vulnerability in LUG Manager, please report it responsibly:

1. **Do NOT** open a public GitHub issue
2. Email the maintainers or use [GitHub's private vulnerability reporting](https://github.com/ArkLUG/lug-manager/security/advisories/new)
3. Include a description of the vulnerability, steps to reproduce, and potential impact

We will acknowledge your report within 48 hours and work on a fix.

## Security Measures

This project implements the following security practices:

### CI/CD Security Pipeline
- **SAST** — GitHub CodeQL scans C++ source for injection, buffer overflow, and logic vulnerabilities
- **SCA** — Trivy scans the Docker image for known CVEs in OS packages and dependencies
- **DAST** — ZAP baseline scan tests the running application for XSS, CSRF, and injection
- **Secret scanning** — TruffleHog checks for committed secrets and credentials
- **Dockerfile linting** — Hadolint validates Dockerfile best practices
- **Static analysis** — cppcheck for C++ code quality and portability issues
- **Test coverage** — lcov + Codecov tracks code coverage across 17 test suites

### Application Security
- **Authentication**: Discord OAuth2 only (no passwords stored)
- **Session management**: HttpOnly cookies with 24-hour expiry
- **PII protection**: Per-field privacy controls with verified member gating
- **Audit logging**: All actions tracked with actor, entity, diff, timestamp, and IP
- **Input validation**: Server-side validation on all form inputs
- **SQL injection prevention**: Parameterized queries throughout (no string concatenation in SQL)
- **XSS prevention**: Mustache templates auto-escape output by default

## Supported Versions

| Version | Supported |
|---------|-----------|
| latest (main) | Yes |
| Older commits | No |

We only support the latest version on the main branch. Please update to the latest before reporting.
