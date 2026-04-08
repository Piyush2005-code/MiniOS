#!/bin/bash
set -e

echo "Waiting for SSH to come up (port 2222)..."

echo "SSH port is open, waiting for sshd to be ready..."
while ! ssh -i ~/.ssh/id_ed25519_cloud -q -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p 2222 ubuntu@localhost 'echo UP' >/dev/null 2>&1; do
  sleep 2
done

echo "SSH is up! Waiting for cloud-init to finish..."
ssh -i ~/.ssh/id_ed25519_cloud -q -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p 2222 ubuntu@localhost 'cloud-init status --wait'

echo "Cloud-init finished! Running setup script..."
ssh -i ~/.ssh/id_ed25519_cloud -q -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p 2222 ubuntu@localhost 'mkdir -p /tmp/ubuntu_bench'
scp -i ~/.ssh/id_ed25519_cloud -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -P 2222 scripts/ubuntu_bench/ubuntu_setup.sh ubuntu@localhost:/tmp/ubuntu_setup.sh
ssh -i ~/.ssh/id_ed25519_cloud -q -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p 2222 ubuntu@localhost 'chmod +x /tmp/ubuntu_setup.sh && sudo /tmp/ubuntu_setup.sh'

echo "Setup done! Transferring scripts and data..."
scp -i ~/.ssh/id_ed25519_cloud -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -r -P 2222 scripts/ubuntu_bench/run_bench.py ubuntu@localhost:/tmp/
scp -i ~/.ssh/id_ed25519_cloud -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -P 2222 src/storage/bench/models/*.onnx ubuntu@localhost:/tmp/
scp -i ~/.ssh/id_ed25519_cloud -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -P 2222 scripts/ubuntu_bench/bench/inputs/*.npy ubuntu@localhost:/tmp/
ssh -i ~/.ssh/id_ed25519_cloud -q -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -p 2222 ubuntu@localhost 'sudo mv /tmp/run_bench.py /bench/ && sudo mv /tmp/*.onnx /bench/models/ && sudo mv /tmp/*.npy /bench/inputs/'

echo "Done!"
