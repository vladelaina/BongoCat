# SignPath 测试签名配置

仓库已在 `.github/workflows/ci.yml` 中接入 SignPath 测试签名。
当前公共 CI 缺少授权的 Cubism SDK，产物是诊断程序；即使已签名，也不能作为最终用户发行版。
本流程不会创建 GitHub Release，也不会使用生产证书。

## 1. 接受邀请

在 https://app.signpath.io 创建个人账号，先接受组织邀请，再确认 CI 用户邮箱。
这两步需要通过发送给你的邮件完成，仓库配置无法代替账号验证。

## 2. 配置 SignPath 后台

1. 创建项目，建议 slug 为 `bongocat`。
2. 添加构件配置，建议 slug 为 `windows-x64-zip`，复制
   [artifact-configuration.xml](../.github/signpath/artifact-configuration.xml) 的完整内容。
3. 在组织中添加预定义的 `GitHub.com` Trusted Build System，并关联此项目及
   `vladelaina/BongoCat` 仓库。按后台提示安装 SignPath GitHub App，并授权此仓库。
4. 创建或使用测试签名策略，建议 slug 为 `test-signing`，选择基金会提供的自签名测试证书。
   将构件配置关联到策略，给已确认邮箱的 CI 用户授予此策略的 Submitter 权限。
5. 从组织页面取得 Organization ID；从测试证书页面取得 SHA-1 指纹（40 位十六进制字符，无空格）。
6. 为 CI 用户生成 API Token，并直接保存到 GitHub Actions Secret。不要提交到仓库或聊天。

XML 匹配三个层级：GitHub 上传 ZIP → `BongoCat-*-windows-x64.zip` →
`BongoCat-*-windows-x64/BongoCat.exe`。同时校验程序的产品名、公司名和原始文件名。
默认要求每个匹配恰好出现一次。不要改成签署任意 EXE 的通配符。

## 3. 配置 GitHub Actions

打开 https://github.com/vladelaina/BongoCat/settings/secrets/actions 。

在 Secrets 中设置：

| 名称 | 值 |
| --- | --- |
| `SIGNPATH_API_TOKEN` | CI 用户的 API Token |

在 Variables 中设置：

| 名称 | 值 |
| --- | --- |
| `SIGNPATH_ORGANIZATION_ID` | 后台组织 ID |
| `SIGNPATH_PROJECT_SLUG` | 实际项目 slug，例如 `bongocat` |
| `SIGNPATH_ARTIFACT_CONFIGURATION_SLUG` | 实际构件配置 slug，例如 `windows-x64-zip` |
| `SIGNPATH_TEST_SIGNING_POLICY_SLUG` | 实际测试策略 slug，例如 `test-signing` |
| `SIGNPATH_TEST_CERTIFICATE_THUMBPRINT` | 测试证书 SHA-1 指纹，40 位十六进制字符 |
| `SIGNPATH_TEST_SIGNING_ENABLED` | 可选；设为 `true` 后，推送 `v*` 标签会自动测试签名 |

未主动启用测试签名时，普通 push 和 PR 仍执行原有构建；PR 不会提交签名请求。
手动勾选签名但缺少必要配置时，任务会明确失败，不会静默生成未签名产物。

## 4. 首次验证

将仓库修改推送后，打开 Actions → BongoCat CI → Run workflow，
选择可信分支，勾选 `sign_windows` 并运行。

流程等待质量检查和全部平台构建通过，再下载本次运行的 Windows 包，
校验原始 SHA256，并重新上传仅含发布 ZIP 的签名输入。
SignPath Action 固定到 v3 的提交，提交请求并等待下载结果。
签名脚本检查包名、条目、未签名资源内容、程序签名及证书指纹，
随后为修改后的 ZIP 重新生成 SHA256。

下载 `test-signed-diagnostic-no-cubism-windows-x64` 产物。其中包含签名后的 ZIP、
新的 `.sha256`、诊断说明和测试证书说明。自签名证书在未信任它的 Windows 上
可能显示 `NotTrusted`；脚本仅在证书指纹匹配且状态为 `Valid` 或 `NotTrusted` 时通过。

可以在本地解压后运行：

```powershell
Get-AuthenticodeSignature ./BongoCat.exe | Format-List Status, StatusMessage, SignerCertificate
Get-FileHash ./BongoCat-0.1.0-windows-x64.zip -Algorithm SHA256
```

若 SignPath 要求人工审批，请在后台审批测试请求；工作流最多等待 20 分钟。
首次成功后，可向基金会提供 GitHub Actions 运行链接、签名请求和构件配置以审核设置。

## 5. 正式证书和最终发行版

基金会审核配置后会订购并导入生产证书。此步骤由基金会完成。
正式发行前还需在 GitHub 托管 runner 上合法获取 Cubism SDK，并以
`-DBONGO_CAT_REQUIRE_CUBISM=ON` 构建最终程序，不能直接发布目前的诊断包。

届时使用独立生产策略、审批规则和受保护发布来源，校验生产证书且要求签名状态为
`Valid`。不要把测试策略变量直接替换成生产策略：本任务的产物仍标注为测试诊断包。

官方参考：

- https://docs.signpath.io/artifact-configuration/
- https://docs.signpath.io/artifact-configuration/syntax
- https://docs.signpath.io/trusted-build-systems/github
