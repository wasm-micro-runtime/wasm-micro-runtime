set -o errexit
set -o pipefail
set -o nounset

echo "Running finalize script..."

#
# Python Package Installation
#
echo "--- Installing Python Dependencies\n"

# Upgrade pip first
python3 -m pip install --no-cache-dir --break-system-packages --upgrade pip
# Install required packages
pip3 install --no-cache-dir --break-system-packages -r .devcontainer/requirements.txt

echo "--- Installing Ocaml stuff\n"
opam init --yes --shell-setup
eval $(opam env --switch=default)
opam install --yes dune menhir

echo "Finalize script completed. ✅"
