# WRCP 25.09 - MOP - LLD Deployment

## Overview
This document outlines the procedures and guidelines for deploying the **Wind River Cloud Platform (WRCP) 25.09** in a **Low-Level Deployment (LLD)** environment, leveraging the knowledge base provided.

## Prerequisites
- Access to WRCP 25.09 documentation
- Properly configured hardware with required specifications
- Network connectivity and access to management interfaces
- Backup of existing configurations prior to deployment

## Deployment Steps

### 1. Initial Setup
1. Ensure all physical interfaces are correctly identified.
2. Verify that any existing backups are up-to-date before proceeding.
3. If a NIC card replacement is required, regenerate the backup post-replacement as PCI addresses will change.
4. Plan for alternative access paths if the OAM interface is affected; maintain serial console access.

### 2. Certificate Management
1. Update the Root CA.
2. After updating the Root CA, send a `SIGHUP` signal to the Vault server process to reload the new certificates.
3. Note that the update of the etcd Root CA is **not supported**.
4. All child certificates signed by the etcd Root CA must be regenerated and updated accordingly.

### 3. Configuration and Deployment
1. Follow the LLD deployment guide for system controller configuration.
2. Secure communication between System Controller and sub-systems using DC admin endpoint certificates.
3. Ensure all components are configured according to the latest WRCP 25.09 specifications.

## Important Notes
- Always backup configurations before making changes.
- For any NIC card replacement, regenerate backups to reflect new PCI addresses.
- Do not attempt to update the etcd Root CA directly; instead, regenerate dependent certificates.
- Maintain serial console access during critical operations involving OAM interface changes.

## References
- [Wind River Cloud Platform 25.09 Concatenated Guides.pdf](#)
- [Cert-Manager GitHub Issue #5851](https://github.com/cert-manager/cert-manager/issues/5851)

---

*This document is generated based on the WRCP 25.09 knowledge base and should be used in conjunction with official documentation for accurate deployment procedures.*