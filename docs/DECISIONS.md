# Implementation decision index

Shared architecture, module contracts and platform design decisions are maintained
in the [BardBox decision index](https://github.com/bardphysicslab/bardbox/blob/main/docs/DECISIONS.md).
The template implements them; do not copy shared specifications here.

| Reference | Owning reasoning | Implementation status |
| --- | --- | --- |
| TEMPLATE-PROVING-1: prove new capabilities in Reference first, then promote reusable implementation into Template | [Accepted direction](https://github.com/bardphysicslab/bardbox-project-template/issues/31#issuecomment-6281614801); [October 21 clarification](https://github.com/bardphysicslab/bardbox-project-template/issues/31#issuecomment-6314220319) | Tracked promotion is part of capability completion; after promotion Reference consumes the maintained implementation through a reviewed dependency boundary. No runtime extraction or dependency mechanism is implemented by these notes |
| BB-001 and BB-002 (shared ingestion decisions) | [BardBox index](https://github.com/bardphysicslab/bardbox/blob/main/docs/DECISIONS.md); existing [ingestion discussion](https://github.com/bardphysicslab/bardbox-project-template/issues/15) | Proposed shared implementation under [milestone](https://github.com/bardphysicslab/bardbox-project-template/issues/16); no implementation claimed by adding this index |

Add implementation-specific decisions only when verified/accepted, with a dated
issue anchor and linked evidence. This initial pointer is not a new decision or
historical backfill. The [shared engineering workflow](https://github.com/bardphysicslab/engineering-standards/blob/main/development-workflow.md)
owns process and proportionality. Form/checker copies shipped here originate in
engineering-standards; assess synchronization when changing them. They prompt/check
completeness only and cannot certify correct architecture or authorization.
