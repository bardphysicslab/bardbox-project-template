FROM python:3.12-slim-bookworm

# g++ is required by tests/test_ota_assignment.py's host-compiled parser
# fixture (built with -fsanitize=address,undefined) -- without it,
# shutil.which('c++') returns None and every test using that fixture
# fails in a clean container that only has this Dockerfile's packages.
RUN apt-get update \
    && apt-get install -y --no-install-recommends git ca-certificates g++ \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspaces/bardbox-project
