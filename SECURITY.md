# Security Policy

## Reporting a security bug in a the WAMR Project
Security is a top priority for the WAMR Project. As such, we take all reports of suspected security vulnerabilities seriously, and have a number of ways to report them.

For suspected vulnerabilities in a specific project, prefer to report the issue using GitHub’s [private vulnerability reporting facilities](https://docs.github.com/en/code-security/security-advisories/guidance-on-reporting-and-writing-information-about-vulnerabilities/privately-reporting-a-security-vulnerability). Maintainers will then work with you to resolve the issue.

If you think that that channel isn’t right for reporting your specific issue, you can also send email to the WAMR core team directly via [wamr-core@googlegroups.com](mailto:wamr-core@googlegroups.com). We will then acknowledge receipt of your report and prioritize initial analysis of severity.

The security team may work in private with individuals from the WAMR Core team, supporting WAMR organizations, core contributors to the affected project, and, where applicable, affected downstream projects and products.

After the initial reply to your report, the core team will endeavor to keep you informed of the progress being made towards a fix and full announcement, and may ask for additional information or guidance surrounding the reported issue.

If you have not received a reply to your report within two days, please reach out again via our email address : [wamr-core@googlegroups.com](mailto:wamr-core@googlegroups.com)


## Preferences
* Please provide detailed reports with reproducible steps and a clearly defined impact.
* Submit one vulnerability per report.
* Social engineering (e.g. phishing, vishing, smishing) is prohibited.

## Disclosure Policy
* The security report is received and is assigned a primary handler. This person will coordinate the fix and release process. The problem is confirmed and a list of all affected versions is determined. Code is audited to find any potential similar problems. Fixes are prepared for all releases which are still under maintenance. These fixes are not committed to the public repository but rather held locally pending the announcement.

* A suggested embargo date for this vulnerability is chosen and a CVE (Common Vulnerabilities and Exposures (CVE®)) is requested for the vulnerability.

* A prenotification may be published on the wamr-dev mailing list, providing information about affected projects, severity, and the embargo date.

* On the embargo date, the wamr-dev mailing list is sent a copy of the announcement. The changes are pushed to the public repository and new builds are deployed.

* Typically the embargo date will be set 72 hours from the time the CVE is issued. However, this may vary depending on the severity of the bug or difficulty in applying a fix.

* This process can take some time, especially when coordination is required with maintainers of other projects. Every effort will be made to handle the bug in as timely a manner as possible; however, it’s important that we follow the release process above to ensure that the disclosure is handled in a consistent manner.

* Core team members are encouraged to write a post-mortem for the WAMR blog, detailing the vulnerability and steps being taken to identify and prevent similar vulnerabilities in the future.

## Receiving Security Updates
Security notifications will be distributed our wamr-dev email list. Please do join this here:
[https://groups.google.com/g/wamr-dev](https://groups.google.com/g/wamr-dev).



