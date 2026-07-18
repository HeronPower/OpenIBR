# Contributing to the Open Inverter-Based Resource

We welcome contributions to the Open Inverter-Based Resource! Whether it's bug reports, feature suggestions, documentation improvements, or algorithm changes, your input helps make this library better. However, to ensure alignment with our roadmap and maintain quality, all contributions must follow our guidelines below.

## Before You Start
- **Review the License**: The project is licensed under the Apache License 2.0 (see [LICENSE](LICENSE)). By contributing, you agree that your contributions will be licensed under the same license.
- **Notify Us First**: To avoid duplicated effort and ensure your idea fits our plans, please open an issue to discuss your proposed changes before submitting a pull request (PR). Describe your idea, motivation, and any relevant details. **For algorithm suggestions, please provide a Simulink model demonstrating the concept or suggestion**.  We'll respond within 5 business days to guide next steps.

## How to Contribute
1. **Report Issues**:
   - Use the [issue tracker](https://github.com/heronpower/OpenIBR/issues) for bugs, questions, or feature requests.
   - Include steps to reproduce, expected vs. actual behavior, and environment details (e.g., target, test conditions).

2. **Suggest Features or Improvements**:
   - Open an issue with a clear title and description. Tag it as "enhancement" if applicable,
   - Include diagrams that help illustrate the proposed change.  For algorithm changes, please show the proposal in Simulink model form,
   - We'll discuss feasibility and prioritize based on our roadmap.

3. **Submit Changes**:
   - Fork the repo and create a topic branch from `main` (e.g., `feature/my-change`),
   - Make small, focused commits with descriptive messages,
   - Ensure code passes any existing tests and matches code structure and style,
   - Submit a PR against the `main` branch, referencing the related issue (e.g., "Fixes #123"),
   - For model changes, submit a detailed summary of changes to facilitate tracking,
   - **Moderation Process**: All PRs are reviewed by Heron Power Electronics Company maintainers. We reserve the right to approve, request changes, or close PRs that don't align with our goals. Expect feedback within 7 days.

## Development Guidelines
- **Design Review**: prioritize diagram-based suggestions, particularly in Simulink.
- **Testing**: Add tests for new features/bug fixes.
- **Documentation**: Update documentation as needed for your changes.
- **Security Issues**: Report privately to openibr@heronpower.com—do not open a public issue.

## What We Don't Accept
- Contributions without prior discussion via an issue.
- Control algorithm changes without diagram-based models.
- Changes that could conflict with our commercial use of the library.
- Large refactors without maintainer approval.

Thank you for contributing! Questions? Reach out via issues or openibr@heronpower.com.