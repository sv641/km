// Create base image for vagrant/virtualbox operations
// Ubuntu image with vagrant/virtualbox preinstalled  (takes 15+ min)

variables {
  aws_region = "us-west-1"
  os = "ubuntu"
  os_version = "20.04"
  ssh_user = "ubuntu"
}

locals {
   target_ami_label = "${var.os} ${var.os_version} base image"
   target_ami_name  = "Kontain_${var.os}_${var.os_version}"
   timestamp = regex_replace(timestamp(), "[- TZ:]", "")
}

source "amazon-ebs" "build" {
  ami_name                    = local.target_ami_name
  ami_description             = local.target_ami_label
  ami_groups                  = ["all"]
  associate_public_ip_address = true
  force_deregister            = true
  instance_type               = "t2.micro"
  region                      = var.aws_region
  source_ami                  = data.amazon-ami.build.id
  ssh_pty                     = true
  ssh_username                = var.ssh_user
  tags = {
    Base_AMI_Name = "{{ .SourceAMIName }}"
    Name          = local.target_ami_label
    OS_Version    = var.os
    Release       = var.os_version
    Timestamp     = local.timestamp
  }
}

build {
  sources = ["source.amazon-ebs.build"]

//   provisioner "shell" {
//     inline = [
//          "echo ===== Waiting for cloud-init to complete...",
//          "if [ -x /usr/bin/cloud-init ] ; then eval 'echo ${var.ssh_password} | sudo /usr/bin/cloud-init status --wait'; fi"
//       ]
//   }

  provisioner "shell" {
    script          = "../azure/scripts/L0-image-provision.sh"
  }
}
