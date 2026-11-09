# Root certificates

Public root CA certificates (Mozilla CA list): ISRG Root X1/X2 (Let's Encrypt),
GTS Root R1/R3/R4 (Google Trust Services), GlobalSign Root R3. Files copied
unchanged from HomeNX (ad3333m/homenx, GPL-3.0) `projects/cards/romfs/certs/`
at commit 0448cd4. They are only tried, still with full verification, for a
host whose chain the console's own certificate store refuses.
