# WRCP Networking Notes

Management network is the operational backbone for subcloud management and platform control traffic.
OAM is the operations/admin access plane and is commonly the endpoint used by administrators and higher-level management clients.

In many WRCP layouts, workers do not require individually exposed IP assignments on every external-facing network.
The design normally follows network function and pool/subnet intent rather than a host-by-host firewall mindset.

When explaining WRCP networking to customers, emphasize:
- the role of subnets and pools
- service reachability by function
- why controller/OAM endpoints differ from internal worker attachment
