# Embedding model from filesystem

Embedding model is now loaded at runtime from:

`/emb/mobilefacenet_u55.bin`

Expected size:

`1127904` bytes

## 1) Export bin on PC

Run in project root:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\export_mobilefacenet_bin.ps1 `
  -InputC .\src\models_classifier\sub_0001_model_data.c `
  -OutputBin .\mobilefacenet_u55.bin
```

## 2) Upload to board filesystem

In MSH:

```sh
mkdir /emb
```

Then use your serial tool to upload `mobilefacenet_u55.bin` to:

`/emb/mobilefacenet_u55.bin`

If your firmware has YMODEM command enabled, in MSH run:

```sh
ry /emb/mobilefacenet_u55.bin
```

Then send `mobilefacenet_u55.bin` from your terminal tool (YMODEM).

## 3) Verify on board

In MSH:

```sh
ls /emb
```

You should see `mobilefacenet_u55.bin` with size around `1127904`.

On next boot, log should show:

`[EMB] model loaded from fs: /emb/mobilefacenet_u55.bin (1127904 bytes)`
