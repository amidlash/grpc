#!/bin/bash -e

# This script is responsible for remotely running tests on an ARM instance.
# It should return a status code useful to the kokoro infrastructure.
# It currently assumes an instance will be selected by the time this script begins running.

if [ -z "$KOKORO_KEYSTORE_DIR" ]; then
    echo "KOKORO_KEYSTORE_DIR is unset. This must be run from kokoro"
    exit 1
fi

IDENTITY=${KOKORO_KEYSTORE_DIR}/73836_grpc_arm_instance_ssh_private_test_key1
AWS_CREDENTIALS=${KOKORO_KEYSTORE_DIR}/73836_grpc_aws_ec2_credentials

# Spawn an instance for running the workflow
## Setup aws cli
# debug linker
curl "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "awscliv2.zip"
unzip awscliv2.zip
sudo ./aws/install 
aws --version

# authenticate with aws cli
mkdir ~/.aws/
echo "[default]" >> ~/.aws/config
ln -s $AWS_CREDENTIALS ~/.aws/credentials

# setup instance 
sudo apt update && sudo apt install -y jq 

# ubuntu 20.04 lts(arm64)
AMI=ami-08e6b682a466887dd
INSTANCE_TYPE=t4g.small
SG=sg-021240e886feba750

ssh-keygen -N '' -t rsa -b 4096 -f ~/.ssh/temp_client_key
ssh-keygen -N '' -t ecdsa -b 256 -f ~/.ssh/temp_server_key
SERVER_PRIVATE_KEY=$(cat ~/.ssh/temp_server_key | sed 's/\(.*\)/    \1/')
SERVER_PUBLIC_KEY=$(cat ~/.ssh/temp_server_key.pub | awk '{print $1 $2 root@localhost}')
CLIENT_PUBLIC_KEY=$(cat ~/.ssh/temp_client_key.pub)

echo '#cloud-config' > userdata
echo 'ssh_authorized_keys:' >> userdata
echo " - $CLIENT_PUBLIC_KEY" >> userdata
echo 'ssh_keys:' >> userdata
echo '  ecdsa_private: |' >> userdata
echo "$SERVER_PRIVATE_KEY" >> userdata
echo '  ecdsa_public: $SERVER_PUBLIC_KEY' >> userdata
echo '' >> userdata
echo 'runcmd:' >> userdata
echo ' - sleep 20m' >> userdata
echo ' - shutdown' >> userdata

cat userdata

# aws ec2 run-instances --image-id ami-064446ad1d755489e --region us-east-2
exit


WORKLOAD=grpc_aws_experiment_remote.sh
chmod 700 $IDENTITY
REMOTE_SCRIPT_FAILURE=0
ssh -i $IDENTITY -o StrictHostKeyChecking=no ubuntu@$INSTANCE "rm -rf grpc"
scp -i $IDENTITY -o StrictHostKeyChecking=no -r github/grpc ubuntu@$INSTANCE:
ssh -i $IDENTITY -o StrictHostKeyChecking=no ubuntu@$INSTANCE "uname -a; ls -l; bash grpc/tools/internal_ci/linux/$WORKLOAD" || REMOTE_SCRIPT_FAILURE=$?

# Sync back sponge_log artifacts (wip)
# echo "looking for sponge logs..."
# find . | grep sponge_log


# Match return value
echo "returning $REMOTE_SCRIPT_FAILURE based on script output"
exit $REMOTE_SCRIPT_FAILURE
