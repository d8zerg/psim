# Справочник Protobuf

<!-- language-links -->
[English](../../../../../contracts/docs/proto-reference.md) | [Русский](proto-reference.md)
<!-- /language-links -->
<a name="top"></a>

## Содержание

- [psim/common/v1/envelope.proto](#psim_common_v1_envelope-proto)
    - [Actor](#psim-common-v1-Actor)
    - [Envelope](#psim-common-v1-Envelope)

    - [ActorKind](#psim-common-v1-ActorKind)

- [psim/audit/v1/audit.proto](#psim_audit_v1_audit-proto)
    - [AuditChainEntry](#psim-audit-v1-AuditChainEntry)
    - [AuditCheckpoint](#psim-audit-v1-AuditCheckpoint)
    - [AuditEntry](#psim-audit-v1-AuditEntry)
    - [AuditEntry.DetailsEntry](#psim-audit-v1-AuditEntry-DetailsEntry)
    - [AuditEventRecord](#psim-audit-v1-AuditEventRecord)

    - [AuditOutcome](#psim-audit-v1-AuditOutcome)

- [psim/api/v1/audit.proto](#psim_api_v1_audit-proto)
    - [AuditQuery](#psim-api-v1-AuditQuery)
    - [ExportAuditRecordsRequest](#psim-api-v1-ExportAuditRecordsRequest)
    - [ExportAuditRecordsResponse](#psim-api-v1-ExportAuditRecordsResponse)
    - [SearchAuditRecordsRequest](#psim-api-v1-SearchAuditRecordsRequest)
    - [SearchAuditRecordsResponse](#psim-api-v1-SearchAuditRecordsResponse)
    - [VerifyAuditIntegrityRequest](#psim-api-v1-VerifyAuditIntegrityRequest)
    - [VerifyAuditIntegrityResponse](#psim-api-v1-VerifyAuditIntegrityResponse)

    - [AuditService](#psim-api-v1-AuditService)

- [psim/common/v1/types.proto](#psim_common_v1_types-proto)
    - [LocationRef](#psim-common-v1-LocationRef)
    - [Position](#psim-common-v1-Position)

    - [EventClass](#psim-common-v1-EventClass)
    - [Priority](#psim-common-v1-Priority)
    - [Severity](#psim-common-v1-Severity)

- [psim/catalog/v1/catalog.proto](#psim_catalog_v1_catalog-proto)
    - [Capability](#psim-catalog-v1-Capability)
    - [CatalogRecord](#psim-catalog-v1-CatalogRecord)
    - [CertificateFingerprint](#psim-catalog-v1-CertificateFingerprint)
    - [Connector](#psim-catalog-v1-Connector)
    - [ConnectorLimits](#psim-catalog-v1-ConnectorLimits)
    - [Device](#psim-catalog-v1-Device)
    - [DeviceType](#psim-catalog-v1-DeviceType)
    - [FloorPlan](#psim-catalog-v1-FloorPlan)
    - [Location](#psim-catalog-v1-Location)
    - [ParameterSpec](#psim-catalog-v1-ParameterSpec)
    - [Source](#psim-catalog-v1-Source)
    - [SourceTemplate](#psim-catalog-v1-SourceTemplate)
    - [StoredFile](#psim-catalog-v1-StoredFile)

    - [LocationKind](#psim-catalog-v1-LocationKind)
    - [ParameterType](#psim-catalog-v1-ParameterType)
    - [ResourceState](#psim-catalog-v1-ResourceState)

- [psim/catalog/v1/maintenance.proto](#psim_catalog_v1_maintenance-proto)
    - [MaintenanceWindow](#psim-catalog-v1-MaintenanceWindow)
    - [SuppressionPolicy](#psim-catalog-v1-SuppressionPolicy)

    - [MaintenanceState](#psim-catalog-v1-MaintenanceState)

- [psim/api/v1/catalog.proto](#psim_api_v1_catalog-proto)
    - [ArchiveLocationRequest](#psim-api-v1-ArchiveLocationRequest)
    - [ArchiveLocationResponse](#psim-api-v1-ArchiveLocationResponse)
    - [CancelMaintenanceWindowRequest](#psim-api-v1-CancelMaintenanceWindowRequest)
    - [CancelMaintenanceWindowResponse](#psim-api-v1-CancelMaintenanceWindowResponse)
    - [ConnectorStatus](#psim-api-v1-ConnectorStatus)
    - [CreateDeviceRequest](#psim-api-v1-CreateDeviceRequest)
    - [CreateDeviceResponse](#psim-api-v1-CreateDeviceResponse)
    - [CreateDeviceTypeRequest](#psim-api-v1-CreateDeviceTypeRequest)
    - [CreateDeviceTypeResponse](#psim-api-v1-CreateDeviceTypeResponse)
    - [CreateLocationRequest](#psim-api-v1-CreateLocationRequest)
    - [CreateLocationResponse](#psim-api-v1-CreateLocationResponse)
    - [CreateMaintenanceWindowRequest](#psim-api-v1-CreateMaintenanceWindowRequest)
    - [CreateMaintenanceWindowResponse](#psim-api-v1-CreateMaintenanceWindowResponse)
    - [ExportDevicesRequest](#psim-api-v1-ExportDevicesRequest)
    - [ExportDevicesResponse](#psim-api-v1-ExportDevicesResponse)
    - [GetConnectorRequest](#psim-api-v1-GetConnectorRequest)
    - [GetConnectorResponse](#psim-api-v1-GetConnectorResponse)
    - [GetDeviceRequest](#psim-api-v1-GetDeviceRequest)
    - [GetDeviceResponse](#psim-api-v1-GetDeviceResponse)
    - [GetDeviceTypeRequest](#psim-api-v1-GetDeviceTypeRequest)
    - [GetDeviceTypeResponse](#psim-api-v1-GetDeviceTypeResponse)
    - [GetFloorPlanRequest](#psim-api-v1-GetFloorPlanRequest)
    - [GetFloorPlanResponse](#psim-api-v1-GetFloorPlanResponse)
    - [GetImportRequest](#psim-api-v1-GetImportRequest)
    - [GetImportResponse](#psim-api-v1-GetImportResponse)
    - [GetLocationRequest](#psim-api-v1-GetLocationRequest)
    - [GetLocationResponse](#psim-api-v1-GetLocationResponse)
    - [GetMaintenanceWindowRequest](#psim-api-v1-GetMaintenanceWindowRequest)
    - [GetMaintenanceWindowResponse](#psim-api-v1-GetMaintenanceWindowResponse)
    - [ImportJob](#psim-api-v1-ImportJob)
    - [ImportRowError](#psim-api-v1-ImportRowError)
    - [ListConnectorsRequest](#psim-api-v1-ListConnectorsRequest)
    - [ListConnectorsResponse](#psim-api-v1-ListConnectorsResponse)
    - [ListDeviceTypesRequest](#psim-api-v1-ListDeviceTypesRequest)
    - [ListDeviceTypesResponse](#psim-api-v1-ListDeviceTypesResponse)
    - [ListDevicesRequest](#psim-api-v1-ListDevicesRequest)
    - [ListDevicesResponse](#psim-api-v1-ListDevicesResponse)
    - [ListFloorPlanVersionsRequest](#psim-api-v1-ListFloorPlanVersionsRequest)
    - [ListFloorPlanVersionsResponse](#psim-api-v1-ListFloorPlanVersionsResponse)
    - [ListLocationsRequest](#psim-api-v1-ListLocationsRequest)
    - [ListLocationsResponse](#psim-api-v1-ListLocationsResponse)
    - [ListMaintenanceWindowsRequest](#psim-api-v1-ListMaintenanceWindowsRequest)
    - [ListMaintenanceWindowsResponse](#psim-api-v1-ListMaintenanceWindowsResponse)
    - [RegisterConnectorRequest](#psim-api-v1-RegisterConnectorRequest)
    - [RegisterConnectorResponse](#psim-api-v1-RegisterConnectorResponse)
    - [RevokeConnectorRequest](#psim-api-v1-RevokeConnectorRequest)
    - [RevokeConnectorResponse](#psim-api-v1-RevokeConnectorResponse)
    - [RotateConnectorCertificateRequest](#psim-api-v1-RotateConnectorCertificateRequest)
    - [RotateConnectorCertificateResponse](#psim-api-v1-RotateConnectorCertificateResponse)
    - [StartImportRequest](#psim-api-v1-StartImportRequest)
    - [StartImportResponse](#psim-api-v1-StartImportResponse)
    - [UpdateDeviceRequest](#psim-api-v1-UpdateDeviceRequest)
    - [UpdateDeviceResponse](#psim-api-v1-UpdateDeviceResponse)
    - [UpdateDeviceTypeRequest](#psim-api-v1-UpdateDeviceTypeRequest)
    - [UpdateDeviceTypeResponse](#psim-api-v1-UpdateDeviceTypeResponse)
    - [UpdateLocationRequest](#psim-api-v1-UpdateLocationRequest)
    - [UpdateLocationResponse](#psim-api-v1-UpdateLocationResponse)
    - [UpdateMaintenanceWindowRequest](#psim-api-v1-UpdateMaintenanceWindowRequest)
    - [UpdateMaintenanceWindowResponse](#psim-api-v1-UpdateMaintenanceWindowResponse)
    - [UploadFloorPlanRequest](#psim-api-v1-UploadFloorPlanRequest)
    - [UploadFloorPlanResponse](#psim-api-v1-UploadFloorPlanResponse)

    - [ImportState](#psim-api-v1-ImportState)

    - [CatalogService](#psim-api-v1-CatalogService)

- [psim/response/v1/command.proto](#psim_response_v1_command-proto)
    - [Command](#psim-response-v1-Command)
    - [Command.ParametersEntry](#psim-response-v1-Command-ParametersEntry)
    - [CommandDelivery](#psim-response-v1-CommandDelivery)
    - [CommandDelivery.ParametersEntry](#psim-response-v1-CommandDelivery-ParametersEntry)
    - [CommandDeliveryRecord](#psim-response-v1-CommandDeliveryRecord)
    - [CommandOutcome](#psim-response-v1-CommandOutcome)
    - [CommandReceipt](#psim-response-v1-CommandReceipt)
    - [CommandResultRecord](#psim-response-v1-CommandResultRecord)

    - [CommandState](#psim-response-v1-CommandState)

- [psim/api/v1/commands.proto](#psim_api_v1_commands-proto)
    - [ApproveCommandRequest](#psim-api-v1-ApproveCommandRequest)
    - [ApproveCommandResponse](#psim-api-v1-ApproveCommandResponse)
    - [CancelCommandRequest](#psim-api-v1-CancelCommandRequest)
    - [CancelCommandResponse](#psim-api-v1-CancelCommandResponse)
    - [GetCommandRequest](#psim-api-v1-GetCommandRequest)
    - [GetCommandResponse](#psim-api-v1-GetCommandResponse)
    - [ListCommandsRequest](#psim-api-v1-ListCommandsRequest)
    - [ListCommandsResponse](#psim-api-v1-ListCommandsResponse)
    - [PreviewCommandRequest](#psim-api-v1-PreviewCommandRequest)
    - [PreviewCommandResponse](#psim-api-v1-PreviewCommandResponse)
    - [RejectCommandRequest](#psim-api-v1-RejectCommandRequest)
    - [RejectCommandResponse](#psim-api-v1-RejectCommandResponse)
    - [RequestCommandRequest](#psim-api-v1-RequestCommandRequest)
    - [RequestCommandRequest.ParametersEntry](#psim-api-v1-RequestCommandRequest-ParametersEntry)
    - [RequestCommandResponse](#psim-api-v1-RequestCommandResponse)

    - [CommandService](#psim-api-v1-CommandService)

- [psim/processing/v1/event.proto](#psim_processing_v1_event-proto)
    - [Event](#psim-processing-v1-Event)
    - [Event.AttributesEntry](#psim-processing-v1-Event-AttributesEntry)
    - [EventRecord](#psim-processing-v1-EventRecord)

- [psim/api/v1/events.proto](#psim_api_v1_events-proto)
    - [GetEventRequest](#psim-api-v1-GetEventRequest)
    - [GetEventResponse](#psim-api-v1-GetEventResponse)
    - [ListIncidentEventsRequest](#psim-api-v1-ListIncidentEventsRequest)
    - [ListIncidentEventsResponse](#psim-api-v1-ListIncidentEventsResponse)
    - [RuleStatistic](#psim-api-v1-RuleStatistic)
    - [RuleStatisticsRequest](#psim-api-v1-RuleStatisticsRequest)
    - [RuleStatisticsResponse](#psim-api-v1-RuleStatisticsResponse)
    - [SearchEventsRequest](#psim-api-v1-SearchEventsRequest)
    - [SearchEventsResponse](#psim-api-v1-SearchEventsResponse)
    - [StoredEvent](#psim-api-v1-StoredEvent)

    - [EventHistoryService](#psim-api-v1-EventHistoryService)

- [psim/api/v1/problem.proto](#psim_api_v1_problem-proto)
    - [FieldViolation](#psim-api-v1-FieldViolation)
    - [Problem](#psim-api-v1-Problem)

- [psim/response/v1/plan.proto](#psim_response_v1_plan-proto)
    - [AutomatedParams](#psim-response-v1-AutomatedParams)
    - [ChecklistItem](#psim-response-v1-ChecklistItem)
    - [ChecklistParams](#psim-response-v1-ChecklistParams)
    - [CommandParams](#psim-response-v1-CommandParams)
    - [CommandParams.ParametersEntry](#psim-response-v1-CommandParams-ParametersEntry)
    - [DecisionOption](#psim-response-v1-DecisionOption)
    - [DecisionParams](#psim-response-v1-DecisionParams)
    - [EscalateParams](#psim-response-v1-EscalateParams)
    - [EscalationLevel](#psim-response-v1-EscalationLevel)
    - [EscalationPolicy](#psim-response-v1-EscalationPolicy)
    - [InstructionParams](#psim-response-v1-InstructionParams)
    - [NotifyParams](#psim-response-v1-NotifyParams)
    - [PlanBinding](#psim-response-v1-PlanBinding)
    - [Recipient](#psim-response-v1-Recipient)
    - [ResponsePlan](#psim-response-v1-ResponsePlan)
    - [SlaTarget](#psim-response-v1-SlaTarget)
    - [Step](#psim-response-v1-Step)
    - [WaitParams](#psim-response-v1-WaitParams)

    - [NotificationChannel](#psim-response-v1-NotificationChannel)
    - [PlanState](#psim-response-v1-PlanState)

- [psim/response/v1/run.proto](#psim_response_v1_run-proto)
    - [ResponseRun](#psim-response-v1-ResponseRun)
    - [StepExecution](#psim-response-v1-StepExecution)
    - [StepResult](#psim-response-v1-StepResult)

    - [RunState](#psim-response-v1-RunState)
    - [StepState](#psim-response-v1-StepState)

- [psim/api/v1/response.proto](#psim_api_v1_response-proto)
    - [ArchivePlanRequest](#psim-api-v1-ArchivePlanRequest)
    - [ArchivePlanResponse](#psim-api-v1-ArchivePlanResponse)
    - [CompleteStepRequest](#psim-api-v1-CompleteStepRequest)
    - [CompleteStepResponse](#psim-api-v1-CompleteStepResponse)
    - [CreateEscalationPolicyRequest](#psim-api-v1-CreateEscalationPolicyRequest)
    - [CreateEscalationPolicyResponse](#psim-api-v1-CreateEscalationPolicyResponse)
    - [GetIncidentRunRequest](#psim-api-v1-GetIncidentRunRequest)
    - [GetIncidentRunResponse](#psim-api-v1-GetIncidentRunResponse)
    - [GetPlanRequest](#psim-api-v1-GetPlanRequest)
    - [GetPlanResponse](#psim-api-v1-GetPlanResponse)
    - [GetSiteDutyRequest](#psim-api-v1-GetSiteDutyRequest)
    - [GetSiteDutyResponse](#psim-api-v1-GetSiteDutyResponse)
    - [ListEscalationPoliciesRequest](#psim-api-v1-ListEscalationPoliciesRequest)
    - [ListEscalationPoliciesResponse](#psim-api-v1-ListEscalationPoliciesResponse)
    - [ListPlansRequest](#psim-api-v1-ListPlansRequest)
    - [ListPlansResponse](#psim-api-v1-ListPlansResponse)
    - [OverrideStepRequest](#psim-api-v1-OverrideStepRequest)
    - [OverrideStepResponse](#psim-api-v1-OverrideStepResponse)
    - [PublishPlanRequest](#psim-api-v1-PublishPlanRequest)
    - [PublishPlanResponse](#psim-api-v1-PublishPlanResponse)
    - [RetryStepRequest](#psim-api-v1-RetryStepRequest)
    - [RetryStepResponse](#psim-api-v1-RetryStepResponse)
    - [SavePlanDraftRequest](#psim-api-v1-SavePlanDraftRequest)
    - [SavePlanDraftResponse](#psim-api-v1-SavePlanDraftResponse)
    - [SetSiteDutyRequest](#psim-api-v1-SetSiteDutyRequest)
    - [SetSiteDutyResponse](#psim-api-v1-SetSiteDutyResponse)
    - [SiteDuty](#psim-api-v1-SiteDuty)
    - [SkipStepRequest](#psim-api-v1-SkipStepRequest)
    - [SkipStepResponse](#psim-api-v1-SkipStepResponse)
    - [UpdateEscalationPolicyRequest](#psim-api-v1-UpdateEscalationPolicyRequest)
    - [UpdateEscalationPolicyResponse](#psim-api-v1-UpdateEscalationPolicyResponse)
    - [ValidatePlanRequest](#psim-api-v1-ValidatePlanRequest)
    - [ValidatePlanResponse](#psim-api-v1-ValidatePlanResponse)

    - [EscalationPolicyService](#psim-api-v1-EscalationPolicyService)
    - [ResponsePlanService](#psim-api-v1-ResponsePlanService)
    - [ResponseRunService](#psim-api-v1-ResponseRunService)
    - [SiteDutyService](#psim-api-v1-SiteDutyService)

- [psim/incident/v1/incident.proto](#psim_incident_v1_incident-proto)
    - [AlarmControl](#psim-incident-v1-AlarmControl)
    - [Incident](#psim-incident-v1-Incident)
    - [IncidentTransfer](#psim-incident-v1-IncidentTransfer)
    - [IncidentType](#psim-incident-v1-IncidentType)
    - [IncidentType.PriorityMatrixEntry](#psim-incident-v1-IncidentType-PriorityMatrixEntry)
    - [Link](#psim-incident-v1-Link)
    - [OperationalState](#psim-incident-v1-OperationalState)
    - [PresenceLease](#psim-incident-v1-PresenceLease)
    - [StepRef](#psim-incident-v1-StepRef)
    - [TimelineEntry](#psim-incident-v1-TimelineEntry)
    - [TimelineEntry.DetailsEntry](#psim-incident-v1-TimelineEntry-DetailsEntry)

    - [IncidentState](#psim-incident-v1-IncidentState)
    - [PrioritySource](#psim-incident-v1-PrioritySource)
    - [Resolution](#psim-incident-v1-Resolution)
    - [TimelineEntryKind](#psim-incident-v1-TimelineEntryKind)

- [psim/api/v1/feed.proto](#psim_api_v1_feed-proto)
    - [ConnectorRuntimeStatus](#psim-api-v1-ConnectorRuntimeStatus)
    - [DeviceStatus](#psim-api-v1-DeviceStatus)
    - [GetIncidentFeedRequest](#psim-api-v1-GetIncidentFeedRequest)
    - [GetIncidentFeedResponse](#psim-api-v1-GetIncidentFeedResponse)
    - [GetLocationStatusRequest](#psim-api-v1-GetLocationStatusRequest)
    - [GetLocationStatusResponse](#psim-api-v1-GetLocationStatusResponse)
    - [GetOperationalSnapshotRequest](#psim-api-v1-GetOperationalSnapshotRequest)
    - [GetOperationalSnapshotResponse](#psim-api-v1-GetOperationalSnapshotResponse)
    - [GetOperatorLoadRequest](#psim-api-v1-GetOperatorLoadRequest)
    - [GetOperatorLoadResponse](#psim-api-v1-GetOperatorLoadResponse)
    - [OperatorLoad](#psim-api-v1-OperatorLoad)
    - [SourceRuntimeStatus](#psim-api-v1-SourceRuntimeStatus)
    - [SourceRuntimeStatus.ValuesEntry](#psim-api-v1-SourceRuntimeStatus-ValuesEntry)
    - [ZoneStatus](#psim-api-v1-ZoneStatus)

    - [DataQuality](#psim-api-v1-DataQuality)

    - [FeedService](#psim-api-v1-FeedService)

- [psim/processing/v1/signal.proto](#psim_processing_v1_signal-proto)
    - [Signal](#psim-processing-v1-Signal)
    - [SignalOrigin](#psim-processing-v1-SignalOrigin)
    - [SignalRecord](#psim-processing-v1-SignalRecord)

    - [SignalOriginKind](#psim-processing-v1-SignalOriginKind)

- [psim/api/v1/incidents.proto](#psim_api_v1_incidents-proto)
    - [AcknowledgeIncidentRequest](#psim-api-v1-AcknowledgeIncidentRequest)
    - [AcknowledgeIncidentResponse](#psim-api-v1-AcknowledgeIncidentResponse)
    - [AddIncidentCommentRequest](#psim-api-v1-AddIncidentCommentRequest)
    - [AddIncidentCommentResponse](#psim-api-v1-AddIncidentCommentResponse)
    - [AddIncidentLinkRequest](#psim-api-v1-AddIncidentLinkRequest)
    - [AddIncidentLinkResponse](#psim-api-v1-AddIncidentLinkResponse)
    - [AssignIncidentRequest](#psim-api-v1-AssignIncidentRequest)
    - [AssignIncidentResponse](#psim-api-v1-AssignIncidentResponse)
    - [CloseIncidentRequest](#psim-api-v1-CloseIncidentRequest)
    - [CloseIncidentResponse](#psim-api-v1-CloseIncidentResponse)
    - [CreateIncidentTypeRequest](#psim-api-v1-CreateIncidentTypeRequest)
    - [CreateIncidentTypeResponse](#psim-api-v1-CreateIncidentTypeResponse)
    - [GetIncidentRequest](#psim-api-v1-GetIncidentRequest)
    - [GetIncidentResponse](#psim-api-v1-GetIncidentResponse)
    - [IncidentSignal](#psim-api-v1-IncidentSignal)
    - [ListIncidentSignalsRequest](#psim-api-v1-ListIncidentSignalsRequest)
    - [ListIncidentSignalsResponse](#psim-api-v1-ListIncidentSignalsResponse)
    - [ListIncidentTypesRequest](#psim-api-v1-ListIncidentTypesRequest)
    - [ListIncidentTypesResponse](#psim-api-v1-ListIncidentTypesResponse)
    - [ListIncidentsRequest](#psim-api-v1-ListIncidentsRequest)
    - [ListIncidentsResponse](#psim-api-v1-ListIncidentsResponse)
    - [ListTimelineRequest](#psim-api-v1-ListTimelineRequest)
    - [ListTimelineResponse](#psim-api-v1-ListTimelineResponse)
    - [ReopenIncidentRequest](#psim-api-v1-ReopenIncidentRequest)
    - [ReopenIncidentResponse](#psim-api-v1-ReopenIncidentResponse)
    - [ResolveIncidentRequest](#psim-api-v1-ResolveIncidentRequest)
    - [ResolveIncidentResponse](#psim-api-v1-ResolveIncidentResponse)
    - [SetIncidentPriorityRequest](#psim-api-v1-SetIncidentPriorityRequest)
    - [SetIncidentPriorityResponse](#psim-api-v1-SetIncidentPriorityResponse)
    - [StartIncidentWorkRequest](#psim-api-v1-StartIncidentWorkRequest)
    - [StartIncidentWorkResponse](#psim-api-v1-StartIncidentWorkResponse)
    - [TransferIncidentItem](#psim-api-v1-TransferIncidentItem)
    - [TransferIncidentResult](#psim-api-v1-TransferIncidentResult)
    - [TransferIncidentsRequest](#psim-api-v1-TransferIncidentsRequest)
    - [TransferIncidentsResponse](#psim-api-v1-TransferIncidentsResponse)
    - [UpdateIncidentTypeRequest](#psim-api-v1-UpdateIncidentTypeRequest)
    - [UpdateIncidentTypeResponse](#psim-api-v1-UpdateIncidentTypeResponse)

    - [IncidentService](#psim-api-v1-IncidentService)

- [psim/api/v1/me.proto](#psim_api_v1_me-proto)
    - [GetMeRequest](#psim-api-v1-GetMeRequest)
    - [GetMeResponse](#psim-api-v1-GetMeResponse)

    - [MeService](#psim-api-v1-MeService)

- [psim/response/v1/notification.proto](#psim_response_v1_notification-proto)
    - [Notification](#psim-response-v1-Notification)
    - [Notification.DataEntry](#psim-response-v1-Notification-DataEntry)

    - [NotificationState](#psim-response-v1-NotificationState)

- [psim/api/v1/notifications.proto](#psim_api_v1_notifications-proto)
    - [ListMyNotificationsRequest](#psim-api-v1-ListMyNotificationsRequest)
    - [ListMyNotificationsResponse](#psim-api-v1-ListMyNotificationsResponse)
    - [MarkNotificationReadRequest](#psim-api-v1-MarkNotificationReadRequest)
    - [MarkNotificationReadResponse](#psim-api-v1-MarkNotificationReadResponse)
    - [NotificationContact](#psim-api-v1-NotificationContact)
    - [ResolveUnknownDeliveryRequest](#psim-api-v1-ResolveUnknownDeliveryRequest)
    - [ResolveUnknownDeliveryResponse](#psim-api-v1-ResolveUnknownDeliveryResponse)
    - [SetNotificationContactRequest](#psim-api-v1-SetNotificationContactRequest)
    - [SetNotificationContactResponse](#psim-api-v1-SetNotificationContactResponse)

    - [NotificationContactService](#psim-api-v1-NotificationContactService)
    - [NotificationService](#psim-api-v1-NotificationService)

- [psim/api/v1/operations.proto](#psim_api_v1_operations-proto)
    - [ActExerciseRequest](#psim-api-v1-ActExerciseRequest)
    - [ActExerciseResponse](#psim-api-v1-ActExerciseResponse)
    - [AskAssistantRequest](#psim-api-v1-AskAssistantRequest)
    - [AskAssistantResponse](#psim-api-v1-AskAssistantResponse)
    - [AssistantClaim](#psim-api-v1-AssistantClaim)
    - [CreateExerciseRequest](#psim-api-v1-CreateExerciseRequest)
    - [CreateExerciseResponse](#psim-api-v1-CreateExerciseResponse)
    - [ExerciseState](#psim-api-v1-ExerciseState)
    - [GetExerciseRequest](#psim-api-v1-GetExerciseRequest)
    - [GetExerciseResponse](#psim-api-v1-GetExerciseResponse)
    - [GetIncidentRecoveryRequest](#psim-api-v1-GetIncidentRecoveryRequest)
    - [GetIncidentRecoveryResponse](#psim-api-v1-GetIncidentRecoveryResponse)
    - [GetIncidentSnapshotsRequest](#psim-api-v1-GetIncidentSnapshotsRequest)
    - [GetIncidentSnapshotsResponse](#psim-api-v1-GetIncidentSnapshotsResponse)
    - [GetOperationsRequest](#psim-api-v1-GetOperationsRequest)
    - [GetOperationsResponse](#psim-api-v1-GetOperationsResponse)
    - [IncidentSnapshot](#psim-api-v1-IncidentSnapshot)
    - [OperationSource](#psim-api-v1-OperationSource)
    - [SetPresenceRequest](#psim-api-v1-SetPresenceRequest)
    - [SetPresenceResponse](#psim-api-v1-SetPresenceResponse)
    - [UpdateAlarmRequest](#psim-api-v1-UpdateAlarmRequest)
    - [UpdateAlarmResponse](#psim-api-v1-UpdateAlarmResponse)

    - [OperationalService](#psim-api-v1-OperationalService)

- [psim/api/v1/reports.proto](#psim_api_v1_reports-proto)
    - [ExportIncidentReportRequest](#psim-api-v1-ExportIncidentReportRequest)
    - [ExportIncidentReportResponse](#psim-api-v1-ExportIncidentReportResponse)
    - [GetIncidentReportRequest](#psim-api-v1-GetIncidentReportRequest)
    - [GetIncidentReportResponse](#psim-api-v1-GetIncidentReportResponse)
    - [IncidentMetrics](#psim-api-v1-IncidentMetrics)
    - [ReportFilter](#psim-api-v1-ReportFilter)

    - [ReportGrouping](#psim-api-v1-ReportGrouping)

    - [ReportService](#psim-api-v1-ReportService)

- [psim/processing/v1/mapping.proto](#psim_processing_v1_mapping-proto)
    - [MappingRule](#psim-processing-v1-MappingRule)
    - [MappingRule.AttributeMapEntry](#psim-processing-v1-MappingRule-AttributeMapEntry)
    - [MappingRuleSet](#psim-processing-v1-MappingRuleSet)

- [psim/processing/v1/rules.proto](#psim_processing_v1_rules-proto)
    - [AbsenceSpec](#psim-processing-v1-AbsenceSpec)
    - [ConjunctionSpec](#psim-processing-v1-ConjunctionSpec)
    - [CorrelationRule](#psim-processing-v1-CorrelationRule)
    - [EventFilter](#psim-processing-v1-EventFilter)
    - [RuleOutput](#psim-processing-v1-RuleOutput)
    - [RuleSet](#psim-processing-v1-RuleSet)
    - [SequenceSpec](#psim-processing-v1-SequenceSpec)
    - [SingleSpec](#psim-processing-v1-SingleSpec)
    - [ThresholdSpec](#psim-processing-v1-ThresholdSpec)

    - [CorrelationScope](#psim-processing-v1-CorrelationScope)
    - [RuleState](#psim-processing-v1-RuleState)

- [psim/api/v1/rules.proto](#psim_api_v1_rules-proto)
    - [ActivateRuleSetRequest](#psim-api-v1-ActivateRuleSetRequest)
    - [ActivateRuleSetResponse](#psim-api-v1-ActivateRuleSetResponse)
    - [ArchiveRuleRequest](#psim-api-v1-ArchiveRuleRequest)
    - [ArchiveRuleResponse](#psim-api-v1-ArchiveRuleResponse)
    - [DryRun](#psim-api-v1-DryRun)
    - [DryRunDecision](#psim-api-v1-DryRunDecision)
    - [GetActiveRuleSetRequest](#psim-api-v1-GetActiveRuleSetRequest)
    - [GetActiveRuleSetResponse](#psim-api-v1-GetActiveRuleSetResponse)
    - [GetDryRunRequest](#psim-api-v1-GetDryRunRequest)
    - [GetDryRunResponse](#psim-api-v1-GetDryRunResponse)
    - [GetMappingRuleSetRequest](#psim-api-v1-GetMappingRuleSetRequest)
    - [GetMappingRuleSetResponse](#psim-api-v1-GetMappingRuleSetResponse)
    - [GetRuleRequest](#psim-api-v1-GetRuleRequest)
    - [GetRuleResponse](#psim-api-v1-GetRuleResponse)
    - [ListMappingRuleSetsRequest](#psim-api-v1-ListMappingRuleSetsRequest)
    - [ListMappingRuleSetsResponse](#psim-api-v1-ListMappingRuleSetsResponse)
    - [ListRulesRequest](#psim-api-v1-ListRulesRequest)
    - [ListRulesResponse](#psim-api-v1-ListRulesResponse)
    - [MaintenanceVersionRef](#psim-api-v1-MaintenanceVersionRef)
    - [PublishMappingRuleSetRequest](#psim-api-v1-PublishMappingRuleSetRequest)
    - [PublishMappingRuleSetResponse](#psim-api-v1-PublishMappingRuleSetResponse)
    - [RuleVersionRef](#psim-api-v1-RuleVersionRef)
    - [SaveRuleDraftRequest](#psim-api-v1-SaveRuleDraftRequest)
    - [SaveRuleDraftResponse](#psim-api-v1-SaveRuleDraftResponse)
    - [StartDryRunRequest](#psim-api-v1-StartDryRunRequest)
    - [StartDryRunResponse](#psim-api-v1-StartDryRunResponse)
    - [ValidateRuleRequest](#psim-api-v1-ValidateRuleRequest)
    - [ValidateRuleResponse](#psim-api-v1-ValidateRuleResponse)

    - [DryRunSource](#psim-api-v1-DryRunSource)
    - [DryRunState](#psim-api-v1-DryRunState)

    - [CorrelationRuleService](#psim-api-v1-CorrelationRuleService)
    - [MappingService](#psim-api-v1-MappingService)

- [psim/config/v1/config.proto](#psim_config_v1_config-proto)
    - [ConfigRecord](#psim-config-v1-ConfigRecord)
    - [ProcessingSettings](#psim-config-v1-ProcessingSettings)
    - [RetentionSettings](#psim-config-v1-RetentionSettings)

- [psim/api/v1/settings.proto](#psim_api_v1_settings-proto)
    - [GetSettingsRequest](#psim-api-v1-GetSettingsRequest)
    - [GetSettingsResponse](#psim-api-v1-GetSettingsResponse)
    - [UpdateProcessingSettingsRequest](#psim-api-v1-UpdateProcessingSettingsRequest)
    - [UpdateProcessingSettingsResponse](#psim-api-v1-UpdateProcessingSettingsResponse)
    - [UpdateRetentionSettingsRequest](#psim-api-v1-UpdateRetentionSettingsRequest)
    - [UpdateRetentionSettingsResponse](#psim-api-v1-UpdateRetentionSettingsResponse)

    - [SettingsService](#psim-api-v1-SettingsService)

- [psim/connector/v1/administration.proto](#psim_connector_v1_administration-proto)
    - [ManageRequest](#psim-connector-v1-ManageRequest)
    - [ManageResponse](#psim-connector-v1-ManageResponse)

    - [RegistrationAction](#psim-connector-v1-RegistrationAction)

    - [ConnectorAdministrationService](#psim-connector-v1-ConnectorAdministrationService)

- [psim/connector/v1/connector.proto](#psim_connector_v1_connector-proto)
    - [BatchAck](#psim-connector-v1-BatchAck)
    - [Command](#psim-connector-v1-Command)
    - [Command.ParametersEntry](#psim-connector-v1-Command-ParametersEntry)
    - [CommandAck](#psim-connector-v1-CommandAck)
    - [CommandResult](#psim-connector-v1-CommandResult)
    - [ConfigUpdate](#psim-connector-v1-ConfigUpdate)
    - [ConnectorDevice](#psim-connector-v1-ConnectorDevice)
    - [ConnectorEvent](#psim-connector-v1-ConnectorEvent)
    - [ConnectorEvent.AttributesEntry](#psim-connector-v1-ConnectorEvent-AttributesEntry)
    - [Credits](#psim-connector-v1-Credits)
    - [EventBatch](#psim-connector-v1-EventBatch)
    - [EventRejection](#psim-connector-v1-EventRejection)
    - [Goodbye](#psim-connector-v1-Goodbye)
    - [Heartbeat](#psim-connector-v1-Heartbeat)
    - [Register](#psim-connector-v1-Register)
    - [Registered](#psim-connector-v1-Registered)
    - [SessionRequest](#psim-connector-v1-SessionRequest)
    - [SessionResponse](#psim-connector-v1-SessionResponse)
    - [SourceCursor](#psim-connector-v1-SourceCursor)
    - [StateObservation](#psim-connector-v1-StateObservation)
    - [StateObservation.ValuesEntry](#psim-connector-v1-StateObservation-ValuesEntry)
    - [StateReport](#psim-connector-v1-StateReport)

    - [DeviceAvailability](#psim-connector-v1-DeviceAvailability)
    - [GoodbyeReason](#psim-connector-v1-GoodbyeReason)

    - [ConnectorGatewayService](#psim-connector-v1-ConnectorGatewayService)

- [psim/incident/v1/events.proto](#psim_incident_v1_events-proto)
    - [IncidentAcknowledged](#psim-incident-v1-IncidentAcknowledged)
    - [IncidentAssigned](#psim-incident-v1-IncidentAssigned)
    - [IncidentBlockingStepsChanged](#psim-incident-v1-IncidentBlockingStepsChanged)
    - [IncidentClosed](#psim-incident-v1-IncidentClosed)
    - [IncidentCommandUpdated](#psim-incident-v1-IncidentCommandUpdated)
    - [IncidentCommentAdded](#psim-incident-v1-IncidentCommentAdded)
    - [IncidentCreated](#psim-incident-v1-IncidentCreated)
    - [IncidentEscalated](#psim-incident-v1-IncidentEscalated)
    - [IncidentEventRecord](#psim-incident-v1-IncidentEventRecord)
    - [IncidentLinkAdded](#psim-incident-v1-IncidentLinkAdded)
    - [IncidentNotificationUpdated](#psim-incident-v1-IncidentNotificationUpdated)
    - [IncidentOperationalUpdated](#psim-incident-v1-IncidentOperationalUpdated)
    - [IncidentPriorityChanged](#psim-incident-v1-IncidentPriorityChanged)
    - [IncidentReopened](#psim-incident-v1-IncidentReopened)
    - [IncidentResolved](#psim-incident-v1-IncidentResolved)
    - [IncidentTransferConfirmed](#psim-incident-v1-IncidentTransferConfirmed)
    - [IncidentTransferRequested](#psim-incident-v1-IncidentTransferRequested)
    - [IncidentWorkStarted](#psim-incident-v1-IncidentWorkStarted)
    - [SignalAttached](#psim-incident-v1-SignalAttached)

- [psim/ingest/v1/raw_event.proto](#psim_ingest_v1_raw_event-proto)
    - [RawEvent](#psim-ingest-v1-RawEvent)
    - [RawEvent.AttributesEntry](#psim-ingest-v1-RawEvent-AttributesEntry)
    - [RawEventRecord](#psim-ingest-v1-RawEventRecord)

- [psim/ingest/v1/state.proto](#psim_ingest_v1_state-proto)
    - [ConnectorConnection](#psim-ingest-v1-ConnectorConnection)
    - [GatewayStateRecord](#psim-ingest-v1-GatewayStateRecord)

- [psim/processing/v1/internal.proto](#psim_processing_v1_internal-proto)
    - [CorrelationChangelogRecord](#psim-processing-v1-CorrelationChangelogRecord)
    - [CorrelationStateEntry](#psim-processing-v1-CorrelationStateEntry)
    - [LiveCandidate](#psim-processing-v1-LiveCandidate)
    - [LiveKeyState](#psim-processing-v1-LiveKeyState)
    - [LivePending](#psim-processing-v1-LivePending)
    - [LiveSource](#psim-processing-v1-LiveSource)
    - [NormalizerStateRecord](#psim-processing-v1-NormalizerStateRecord)
    - [RepartitionRecord](#psim-processing-v1-RepartitionRecord)
    - [RepartitionedEvent](#psim-processing-v1-RepartitionedEvent)
    - [SourceSequenceState](#psim-processing-v1-SourceSequenceState)

- [psim/response/v1/events.proto](#psim_response_v1_events-proto)
    - [CommandRequested](#psim-response-v1-CommandRequested)
    - [CommandUpdated](#psim-response-v1-CommandUpdated)
    - [EscalationLevelTriggered](#psim-response-v1-EscalationLevelTriggered)
    - [NotificationRequested](#psim-response-v1-NotificationRequested)
    - [NotificationUpdated](#psim-response-v1-NotificationUpdated)
    - [PlanUnavailable](#psim-response-v1-PlanUnavailable)
    - [ResponseEventRecord](#psim-response-v1-ResponseEventRecord)
    - [RunUpdated](#psim-response-v1-RunUpdated)
    - [SiteDutyChanged](#psim-response-v1-SiteDutyChanged)
    - [SlaBreached](#psim-response-v1-SlaBreached)
    - [SlaPolicyPinned](#psim-response-v1-SlaPolicyPinned)

    - [RunChange](#psim-response-v1-RunChange)

- [psim/realtime/v1/realtime.proto](#psim_realtime_v1_realtime-proto)
    - [Authenticate](#psim-realtime-v1-Authenticate)
    - [Authenticated](#psim-realtime-v1-Authenticated)
    - [ClientFrame](#psim-realtime-v1-ClientFrame)
    - [Delta](#psim-realtime-v1-Delta)
    - [ErrorFrame](#psim-realtime-v1-ErrorFrame)
    - [FeedFilter](#psim-realtime-v1-FeedFilter)
    - [Ping](#psim-realtime-v1-Ping)
    - [Pong](#psim-realtime-v1-Pong)
    - [ResyncRequired](#psim-realtime-v1-ResyncRequired)
    - [ServerFrame](#psim-realtime-v1-ServerFrame)
    - [Subscribe](#psim-realtime-v1-Subscribe)
    - [Subscribed](#psim-realtime-v1-Subscribed)
    - [Unsubscribe](#psim-realtime-v1-Unsubscribe)

    - [Stream](#psim-realtime-v1-Stream)

- [Скалярные типы](#scalar-value-types)



<a name="psim_common_v1_envelope-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/common/v1/envelope.proto



<a name="psim-common-v1-Actor"></a>

### Actor
Actor определяет субъекта, выполнившего действие.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| kind | [ActorKind](#psim-common-v1-ActorKind) |  |  |
| id | [string](#string) |  | Идентификатор пользователя, имя сервиса, идентификатор коннектора или &quot;system&quot;. |
| display_name | [string](#string) |  |  |






<a name="psim-common-v1-Envelope"></a>

### Envelope
Envelope содержит общие метаданные каждого сообщения во всех топиках PSIM.
См. ADR-005. Тип содержимого определяется oneof записи топика (ADR-004).


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| message_id | [string](#string) |  | Идентификатор сообщения: event_id, signal_id или идентификатор доменного события, UUIDv7 или UUIDv5. |
| tenant_id | [string](#string) |  |  |
| occurred_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  | Время факта в предметной области, event time. |
| produced_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  | Время записи сообщения в Kafka. |
| received_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  | Время приёма события платформой, только для входящих событий. |
| sequence | [uint64](#uint64) |  | Порядковый номер источника для событий или версия агрегата для доменных событий. |
| causation_id | [string](#string) |  | Сообщение, вызвавшее текущее. |
| correlation_id | [string](#string) |  | Идентификатор исходного события всей причинной цепочки. |
| actor | [Actor](#psim-common-v1-Actor) |  |  |
| schema_version | [string](#string) |  | Версия выпуска контрактов, сформировавшего сообщение, для диагностики. |








<a name="psim-common-v1-ActorKind"></a>

### ActorKind
ActorKind - вид субъекта.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| ACTOR_KIND_UNSPECIFIED | 0 |  |
| ACTOR_KIND_USER | 1 |  |
| ACTOR_KIND_SERVICE | 2 |  |
| ACTOR_KIND_CONNECTOR | 3 |  |
| ACTOR_KIND_SYSTEM | 4 |  |










<a name="psim_audit_v1_audit-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/audit/v1/audit.proto



<a name="psim-audit-v1-AuditChainEntry"></a>

### AuditChainEntry
AuditChainEntry - запись аудита в виде, сохранённом в цепочке.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| audit_seq | [uint64](#uint64) |  |  |
| source_event_id | [string](#string) |  |  |
| entry | [AuditEntry](#psim-audit-v1-AuditEntry) |  |  |
| occurred_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| recorded_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| prev_hash | [bytes](#bytes) |  |  |
| hash | [bytes](#bytes) |  |  |
| tenant_id | [string](#string) |  | Тенант включён в канонические данные для хэширования. |






<a name="psim-audit-v1-AuditCheckpoint"></a>

### AuditCheckpoint
AuditCheckpoint фиксирует конец цепочки (ADR-021).


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| audit_seq | [uint64](#uint64) |  |  |
| hash | [bytes](#bytes) |  |  |
| created_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| signature | [bytes](#bytes) |  |  |
| key_id | [string](#string) |  |  |
| tenant_id | [string](#string) |  | Тенант включён в подписываемые данные контрольной точки. |






<a name="psim-audit-v1-AuditEntry"></a>

### AuditEntry
AuditEntry - действие для записи в цепочку аудита.
envelope.causation_id - идентификатор исходного доменного события, ключ идемпотентности (AU3).


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| actor | [psim.common.v1.Actor](#psim-common-v1-Actor) |  |  |
| action | [string](#string) |  | Имя действия с точками, например &quot;incident.acknowledge&quot;, &quot;access.denied&quot;. |
| object_type | [string](#string) |  |  |
| object_id | [string](#string) |  |  |
| site_id | [string](#string) |  |  |
| outcome | [AuditOutcome](#psim-audit-v1-AuditOutcome) |  |  |
| details | [AuditEntry.DetailsEntry](#psim-audit-v1-AuditEntry-DetailsEntry) | repeated |  |
| client_address | [string](#string) |  | Адрес клиента и user agent для действий пользователя. |
| user_agent | [string](#string) |  |  |






<a name="psim-audit-v1-AuditEntry-DetailsEntry"></a>

### AuditEntry.DetailsEntry



| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-audit-v1-AuditEventRecord"></a>

### AuditEventRecord
AuditEventRecord - значение топика psim.audit.v1 с ключом tenant_id.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| audit_entry | [AuditEntry](#psim-audit-v1-AuditEntry) |  |  |
| audit_checkpoint | [AuditCheckpoint](#psim-audit-v1-AuditCheckpoint) |  |  |








<a name="psim-audit-v1-AuditOutcome"></a>

### AuditOutcome
AuditOutcome - результат аудируемого действия.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| AUDIT_OUTCOME_UNSPECIFIED | 0 |  |
| AUDIT_OUTCOME_SUCCESS | 1 |  |
| AUDIT_OUTCOME_DENIED | 2 |  |
| AUDIT_OUTCOME_FAILED | 3 |  |










<a name="psim_api_v1_audit-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/api/v1/audit.proto



<a name="psim-api-v1-AuditQuery"></a>

### AuditQuery
AuditQuery выбирает записи аудита.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| from | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| to | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| actor_id | [string](#string) |  |  |
| action | [string](#string) |  | Префикс действия, например &quot;incident.&quot;. |
| object_type | [string](#string) |  |  |
| object_id | [string](#string) |  |  |
| site_id | [string](#string) |  |  |
| outcome | [psim.audit.v1.AuditOutcome](#psim-audit-v1-AuditOutcome) |  |  |






<a name="psim-api-v1-ExportAuditRecordsRequest"></a>

### ExportAuditRecordsRequest
ExportAuditRecordsRequest - запрос операции ExportAuditRecords.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| query | [AuditQuery](#psim-api-v1-AuditQuery) |  |  |






<a name="psim-api-v1-ExportAuditRecordsResponse"></a>

### ExportAuditRecordsResponse
ExportAuditRecordsResponse - ответ операции ExportAuditRecords; тело имеет формат JSON lines.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| file | [google.api.HttpBody](#google-api-HttpBody) |  |  |






<a name="psim-api-v1-SearchAuditRecordsRequest"></a>

### SearchAuditRecordsRequest
SearchAuditRecordsRequest - запрос операции SearchAuditRecords.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| query | [AuditQuery](#psim-api-v1-AuditQuery) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-SearchAuditRecordsResponse"></a>

### SearchAuditRecordsResponse
SearchAuditRecordsResponse - ответ операции SearchAuditRecords.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| records | [psim.audit.v1.AuditChainEntry](#psim-audit-v1-AuditChainEntry) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-VerifyAuditIntegrityRequest"></a>

### VerifyAuditIntegrityRequest
VerifyAuditIntegrityRequest - запрос операции VerifyAuditIntegrity.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| from_seq | [uint64](#uint64) |  | 0 означает начало с первой записи. |
| to_seq | [uint64](#uint64) |  | 0 означает конец на последней записи. |






<a name="psim-api-v1-VerifyAuditIntegrityResponse"></a>

### VerifyAuditIntegrityResponse
VerifyAuditIntegrityResponse - ответ операции VerifyAuditIntegrity.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| intact | [bool](#bool) |  |  |
| records_checked | [uint64](#uint64) |  |  |
| first_broken_seq | [uint64](#uint64) |  | Первая запись с нарушением цепочки; 0, если цепочка цела. |
| detail | [string](#string) |  |  |
| last_checkpoint | [psim.audit.v1.AuditCheckpoint](#psim-audit-v1-AuditCheckpoint) |  |  |












<a name="psim-api-v1-AuditService"></a>

### AuditService
AuditService ищет и проверяет цепочку аудита (ADR-021). Владелец: Audit Service.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| SearchAuditRecords | [SearchAuditRecordsRequest](#psim-api-v1-SearchAuditRecordsRequest) | [SearchAuditRecordsResponse](#psim-api-v1-SearchAuditRecordsResponse) | SearchAuditRecords ищет записи аудита, начиная с самых новых. |
| VerifyAuditIntegrity | [VerifyAuditIntegrityRequest](#psim-api-v1-VerifyAuditIntegrityRequest) | [VerifyAuditIntegrityResponse](#psim-api-v1-VerifyAuditIntegrityResponse) | VerifyAuditIntegrity пересчитывает цепочку для диапазона записей. |
| ExportAuditRecords | [ExportAuditRecordsRequest](#psim-api-v1-ExportAuditRecordsRequest) | [ExportAuditRecordsResponse](#psim-api-v1-ExportAuditRecordsResponse) | ExportAuditRecords экспортирует записи с соседними хэшами и контрольными точками в формате JSON lines. |





<a name="psim_common_v1_types-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/common/v1/types.proto



<a name="psim-common-v1-LocationRef"></a>

### LocationRef
LocationRef размещает объект в иерархии площадки.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| site_id | [string](#string) |  |  |
| zone_id | [string](#string) |  |  |






<a name="psim-common-v1-Position"></a>

### Position
Position - точка на поэтажном плане в единицах плана.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| x | [double](#double) |  |  |
| y | [double](#double) |  |  |








<a name="psim-common-v1-EventClass"></a>

### EventClass
EventClass - поведенческий класс типа таксономии.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| EVENT_CLASS_UNSPECIFIED | 0 |  |
| EVENT_CLASS_ALARM | 1 |  |
| EVENT_CLASS_WARNING | 2 |  |
| EVENT_CLASS_FAULT | 3 |  |
| EVENT_CLASS_RESTORE | 4 |  |
| EVENT_CLASS_STATE | 5 |  |
| EVENT_CLASS_INFO | 6 |  |



<a name="psim-common-v1-Priority"></a>

### Priority
Приоритет сигнала или инцидента; P1 - наивысший.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| PRIORITY_UNSPECIFIED | 0 |  |
| PRIORITY_P1 | 1 |  |
| PRIORITY_P2 | 2 |  |
| PRIORITY_P3 | 3 |  |
| PRIORITY_P4 | 4 |  |



<a name="psim-common-v1-Severity"></a>

### Severity
Критичность отдельного события.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| SEVERITY_UNSPECIFIED | 0 |  |
| SEVERITY_INFO | 1 |  |
| SEVERITY_LOW | 2 |  |
| SEVERITY_MEDIUM | 3 |  |
| SEVERITY_HIGH | 4 |  |
| SEVERITY_CRITICAL | 5 |  |










<a name="psim_catalog_v1_catalog-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/catalog/v1/catalog.proto



<a name="psim-catalog-v1-Capability"></a>

### Capability
Capability - команда или режим, поддерживаемые устройством.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| code | [string](#string) |  | Например, &quot;lock&quot;, &quot;unlock&quot;, &quot;arm&quot;, &quot;ptz.preset&quot;. |
| name | [string](#string) |  |  |
| critical | [bool](#bool) |  | Критичные команды требуют одобрения вторым пользователем (CM3). |
| consequence | [string](#string) |  | Последствие для оператора, настроенное администратором; пустое значение означает неизвестное последствие. |
| parameters | [ParameterSpec](#psim-catalog-v1-ParameterSpec) | repeated |  |






<a name="psim-catalog-v1-CatalogRecord"></a>

### CatalogRecord
CatalogRecord - значение компактируемого топика psim.catalog.v1
Ключ: resource_type:resource_id. Каждая запись содержит полное текущее состояние ресурса.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| location | [Location](#psim-catalog-v1-Location) |  |  |
| device_type | [DeviceType](#psim-catalog-v1-DeviceType) |  |  |
| device | [Device](#psim-catalog-v1-Device) |  |  |
| connector | [Connector](#psim-catalog-v1-Connector) |  |  |
| stored_file | [StoredFile](#psim-catalog-v1-StoredFile) |  |  |






<a name="psim-catalog-v1-CertificateFingerprint"></a>

### CertificateFingerprint
CertificateFingerprint привязывает клиентский сертификат mTLS к коннектору.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| sha256 | [string](#string) |  | SHA-256 сертификата DER в шестнадцатеричном представлении. |
| not_after | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-catalog-v1-Connector"></a>

### Connector
Connector - зарегистрированный коннектор с учётными данными и лимитами.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| connector_id | [string](#string) |  |  |
| name | [string](#string) |  |  |
| connector_type | [string](#string) |  | Выбирает набор правил нормализации, например &quot;simulator&quot; или &quot;mqtt&quot;. |
| certificates | [CertificateFingerprint](#psim-catalog-v1-CertificateFingerprint) | repeated |  |
| limits | [ConnectorLimits](#psim-catalog-v1-ConnectorLimits) |  |  |
| state | [ResourceState](#psim-catalog-v1-ResourceState) |  |  |
| version | [uint64](#uint64) |  |  |






<a name="psim-catalog-v1-ConnectorLimits"></a>

### ConnectorLimits
ConnectorLimits - ограничения приёма событий для каждого коннектора, применяемые шлюзом.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| max_events_per_second | [uint32](#uint32) |  |  |
| max_batch_events | [uint32](#uint32) |  |  |






<a name="psim-catalog-v1-Device"></a>

### Device
Device - физическое или логическое устройство с его источниками.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| device_id | [string](#string) |  |  |
| device_type_id | [string](#string) |  |  |
| zone_id | [string](#string) |  |  |
| site_id | [string](#string) |  |  |
| connector_id | [string](#string) |  |  |
| external_id | [string](#string) |  | Идентификатор устройства во внешней системе, уникальный в пределах коннектора (D1). |
| name | [string](#string) |  |  |
| sources | [Source](#psim-catalog-v1-Source) | repeated |  |
| position | [psim.common.v1.Position](#psim-common-v1-Position) |  |  |
| state | [ResourceState](#psim-catalog-v1-ResourceState) |  |  |
| version | [uint64](#uint64) |  |  |
| updated_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-catalog-v1-DeviceType"></a>

### DeviceType
DeviceType описывает класс устройств, его источники и capabilities.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| device_type_id | [string](#string) |  |  |
| code | [string](#string) |  |  |
| name | [string](#string) |  |  |
| taxonomy_domain | [string](#string) |  | Домен таксономии, например &quot;access&quot;. |
| source_templates | [SourceTemplate](#psim-catalog-v1-SourceTemplate) | repeated |  |
| capabilities | [Capability](#psim-catalog-v1-Capability) | repeated |  |
| state | [ResourceState](#psim-catalog-v1-ResourceState) |  |  |
| version | [uint64](#uint64) |  |  |






<a name="psim-catalog-v1-FloorPlan"></a>

### FloorPlan
FloorPlan - изображение этажа с системой координат для размещения устройств.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| image_ref | [string](#string) |  | Ссылка на сохранённое изображение, доступное через GET /v1/locations/{id}/floor-plan. |
| content_type | [string](#string) |  |  |
| width | [double](#double) |  |  |
| height | [double](#double) |  |  |
| file | [StoredFile](#psim-catalog-v1-StoredFile) |  |  |






<a name="psim-catalog-v1-Location"></a>

### Location
Location - площадка, здание, этаж или зона.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| location_id | [string](#string) |  |  |
| kind | [LocationKind](#psim-catalog-v1-LocationKind) |  |  |
| parent_id | [string](#string) |  | Пустое значение допускается только для площадок. |
| site_id | [string](#string) |  |  |
| name | [string](#string) |  |  |
| code | [string](#string) |  | Уникально среди объектов с одним родителем. |
| timezone | [string](#string) |  | Часовой пояс IANA, только для площадок. |
| floor_plan | [FloorPlan](#psim-catalog-v1-FloorPlan) |  | Только для этажей. |
| state | [ResourceState](#psim-catalog-v1-ResourceState) |  |  |
| version | [uint64](#uint64) |  |  |
| updated_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-catalog-v1-ParameterSpec"></a>

### ParameterSpec
ParameterSpec описывает один параметр команды.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| name | [string](#string) |  |  |
| type | [ParameterType](#psim-catalog-v1-ParameterType) |  |  |
| required | [bool](#bool) |  |  |
| allowed_values | [string](#string) | repeated |  |






<a name="psim-catalog-v1-Source"></a>

### Source
Source - канал событий устройства.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| source_id | [string](#string) |  | Детерминированный UUIDv5(connector_id, external_ref), см. ADR-029. |
| external_ref | [string](#string) |  | Ссылка, используемая коннектором, уникальная в его пределах (D2). |
| kind | [string](#string) |  |  |
| name | [string](#string) |  |  |






<a name="psim-catalog-v1-SourceTemplate"></a>

### SourceTemplate
SourceTemplate описывает источник, имеющийся у каждого устройства данного типа.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| kind | [string](#string) |  |  |
| name | [string](#string) |  |  |






<a name="psim-catalog-v1-StoredFile"></a>

### StoredFile
StoredFile описывает неизменяемый закрытый объект, принадлежащий этажу.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| file_id | [string](#string) |  |  |
| site_id | [string](#string) |  |  |
| owner_id | [string](#string) |  |  |
| version | [uint64](#uint64) |  |  |
| size | [uint64](#uint64) |  |  |
| content_type | [string](#string) |  |  |
| sha256 | [string](#string) |  |  |
| object_key | [string](#string) |  | Внутренний идентификатор хранилища; клиенты скачивают файл через авторизованный endpoint этажа. |
| created_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| pixel_width | [uint32](#uint32) |  |  |
| pixel_height | [uint32](#uint32) |  |  |








<a name="psim-catalog-v1-LocationKind"></a>

### LocationKind
LocationKind - уровень локации в иерархии.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| LOCATION_KIND_UNSPECIFIED | 0 |  |
| LOCATION_KIND_SITE | 1 |  |
| LOCATION_KIND_BUILDING | 2 |  |
| LOCATION_KIND_FLOOR | 3 |  |
| LOCATION_KIND_ZONE | 4 |  |



<a name="psim-catalog-v1-ParameterType"></a>

### ParameterType
ParameterType - тип параметра команды.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| PARAMETER_TYPE_UNSPECIFIED | 0 |  |
| PARAMETER_TYPE_STRING | 1 |  |
| PARAMETER_TYPE_INTEGER | 2 |  |
| PARAMETER_TYPE_NUMBER | 3 |  |
| PARAMETER_TYPE_BOOLEAN | 4 |  |
| PARAMETER_TYPE_DURATION | 5 |  |



<a name="psim-catalog-v1-ResourceState"></a>

### ResourceState
ResourceState - общее состояние жизненного цикла ресурсов каталога.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| RESOURCE_STATE_UNSPECIFIED | 0 |  |
| RESOURCE_STATE_ACTIVE | 1 |  |
| RESOURCE_STATE_DISABLED | 2 |  |
| RESOURCE_STATE_ARCHIVED | 3 |  |










<a name="psim_catalog_v1_maintenance-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/catalog/v1/maintenance.proto



<a name="psim-catalog-v1-MaintenanceWindow"></a>

### MaintenanceWindow
MaintenanceWindow - однократный интервал, проверяемый по времени приёма события.
Пожарные события и сигналы P1 никогда не подавляются; история и открытые инциденты сохраняются.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| maintenance_id | [string](#string) |  |  |
| site_id | [string](#string) |  |  |
| device_id | [string](#string) |  |  |
| zone_id | [string](#string) |  |  |
| starts_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| ends_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| reason | [string](#string) |  |  |
| responsible | [string](#string) |  |  |
| suppression | [SuppressionPolicy](#psim-catalog-v1-SuppressionPolicy) |  |  |
| state | [MaintenanceState](#psim-catalog-v1-MaintenanceState) |  |  |
| version | [uint64](#uint64) |  |  |
| updated_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| activated_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| completed_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-catalog-v1-SuppressionPolicy"></a>

### SuppressionPolicy
SuppressionPolicy явно выбирает типы событий или правила для подавления новых сигналов.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| event_types | [string](#string) | repeated |  |
| rule_ids | [string](#string) | repeated |  |








<a name="psim-catalog-v1-MaintenanceState"></a>

### MaintenanceState
MaintenanceState сохраняется, версионируется и публикуется при каждом переходе.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| MAINTENANCE_STATE_UNSPECIFIED | 0 |  |
| MAINTENANCE_STATE_SCHEDULED | 1 |  |
| MAINTENANCE_STATE_ACTIVE | 2 |  |
| MAINTENANCE_STATE_COMPLETED | 3 |  |
| MAINTENANCE_STATE_CANCELLED | 4 |  |










<a name="psim_api_v1_catalog-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/api/v1/catalog.proto



<a name="psim-api-v1-ArchiveLocationRequest"></a>

### ArchiveLocationRequest
ArchiveLocationRequest - запрос операции ArchiveLocation.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| location_id | [string](#string) |  |  |






<a name="psim-api-v1-ArchiveLocationResponse"></a>

### ArchiveLocationResponse
ArchiveLocationResponse - ответ операции ArchiveLocation.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| location | [psim.catalog.v1.Location](#psim-catalog-v1-Location) |  |  |






<a name="psim-api-v1-CancelMaintenanceWindowRequest"></a>

### CancelMaintenanceWindowRequest
CancelMaintenanceWindowRequest определяет интервал.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| maintenance_id | [string](#string) |  |  |






<a name="psim-api-v1-CancelMaintenanceWindowResponse"></a>

### CancelMaintenanceWindowResponse
CancelMaintenanceWindowResponse включает конечную версию.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| maintenance_window | [psim.catalog.v1.MaintenanceWindow](#psim-catalog-v1-MaintenanceWindow) |  |  |






<a name="psim-api-v1-ConnectorStatus"></a>

### ConnectorStatus
ConnectorStatus - оперативное состояние коннектора, полученное из реестра сессий.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| connector | [psim.catalog.v1.Connector](#psim-catalog-v1-Connector) |  |  |
| online | [bool](#bool) |  |  |
| last_seen_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| events_per_second | [double](#double) |  |  |
| rejected_events_total | [uint64](#uint64) |  |  |
| dlq_events_total | [uint64](#uint64) |  |  |






<a name="psim-api-v1-CreateDeviceRequest"></a>

### CreateDeviceRequest
CreateDeviceRequest - запрос операции CreateDevice.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| device | [psim.catalog.v1.Device](#psim-catalog-v1-Device) |  |  |






<a name="psim-api-v1-CreateDeviceResponse"></a>

### CreateDeviceResponse
CreateDeviceResponse - ответ операции CreateDevice.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| device | [psim.catalog.v1.Device](#psim-catalog-v1-Device) |  |  |






<a name="psim-api-v1-CreateDeviceTypeRequest"></a>

### CreateDeviceTypeRequest
CreateDeviceTypeRequest - запрос операции CreateDeviceType.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| device_type | [psim.catalog.v1.DeviceType](#psim-catalog-v1-DeviceType) |  |  |






<a name="psim-api-v1-CreateDeviceTypeResponse"></a>

### CreateDeviceTypeResponse
CreateDeviceTypeResponse - ответ операции CreateDeviceType.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| device_type | [psim.catalog.v1.DeviceType](#psim-catalog-v1-DeviceType) |  |  |






<a name="psim-api-v1-CreateLocationRequest"></a>

### CreateLocationRequest
CreateLocationRequest - запрос операции CreateLocation.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| location | [psim.catalog.v1.Location](#psim-catalog-v1-Location) |  |  |






<a name="psim-api-v1-CreateLocationResponse"></a>

### CreateLocationResponse
CreateLocationResponse - ответ операции CreateLocation.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| location | [psim.catalog.v1.Location](#psim-catalog-v1-Location) |  |  |






<a name="psim-api-v1-CreateMaintenanceWindowRequest"></a>

### CreateMaintenanceWindowRequest
CreateMaintenanceWindowRequest содержит желаемый интервал.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| maintenance_window | [psim.catalog.v1.MaintenanceWindow](#psim-catalog-v1-MaintenanceWindow) |  |  |






<a name="psim-api-v1-CreateMaintenanceWindowResponse"></a>

### CreateMaintenanceWindowResponse
CreateMaintenanceWindowResponse включает назначенную версию.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| maintenance_window | [psim.catalog.v1.MaintenanceWindow](#psim-catalog-v1-MaintenanceWindow) |  |  |






<a name="psim-api-v1-ExportDevicesRequest"></a>

### ExportDevicesRequest
ExportDevicesRequest - запрос операции ExportDevices.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| location_id | [string](#string) |  |  |
| format | [string](#string) |  |  |






<a name="psim-api-v1-ExportDevicesResponse"></a>

### ExportDevicesResponse
ExportDevicesResponse - ответ операции ExportDevices; тело содержит файл.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| file | [google.api.HttpBody](#google-api-HttpBody) |  |  |






<a name="psim-api-v1-GetConnectorRequest"></a>

### GetConnectorRequest
GetConnectorRequest определяет регистрацию.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| connector_id | [string](#string) |  |  |






<a name="psim-api-v1-GetConnectorResponse"></a>

### GetConnectorResponse
GetConnectorResponse содержит последнюю версию регистрации из проекции.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| connector | [psim.catalog.v1.Connector](#psim-catalog-v1-Connector) |  |  |






<a name="psim-api-v1-GetDeviceRequest"></a>

### GetDeviceRequest
GetDeviceRequest - запрос операции GetDevice.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| device_id | [string](#string) |  |  |






<a name="psim-api-v1-GetDeviceResponse"></a>

### GetDeviceResponse
GetDeviceResponse - ответ операции GetDevice.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| device | [psim.catalog.v1.Device](#psim-catalog-v1-Device) |  |  |
| device_type | [psim.catalog.v1.DeviceType](#psim-catalog-v1-DeviceType) |  | Capabilities раскрываются в пределах области доступа разрешённого устройства. |






<a name="psim-api-v1-GetDeviceTypeRequest"></a>

### GetDeviceTypeRequest
GetDeviceTypeRequest определяет тип устройства.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| device_type_id | [string](#string) |  |  |






<a name="psim-api-v1-GetDeviceTypeResponse"></a>

### GetDeviceTypeResponse
GetDeviceTypeResponse содержит текущую версию.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| device_type | [psim.catalog.v1.DeviceType](#psim-catalog-v1-DeviceType) |  |  |






<a name="psim-api-v1-GetFloorPlanRequest"></a>

### GetFloorPlanRequest
GetFloorPlanRequest - запрос операции GetFloorPlan.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| location_id | [string](#string) |  |  |
| version | [uint64](#uint64) |  | Ноль выбирает текущую привязанную версию. |






<a name="psim-api-v1-GetFloorPlanResponse"></a>

### GetFloorPlanResponse
GetFloorPlanResponse - ответ операции GetFloorPlan; тело содержит изображение.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| image | [google.api.HttpBody](#google-api-HttpBody) |  |  |






<a name="psim-api-v1-GetImportRequest"></a>

### GetImportRequest
GetImportRequest - запрос операции GetImport.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| import_id | [string](#string) |  |  |






<a name="psim-api-v1-GetImportResponse"></a>

### GetImportResponse
GetImportResponse - ответ операции GetImport.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| import_job | [ImportJob](#psim-api-v1-ImportJob) |  |  |






<a name="psim-api-v1-GetLocationRequest"></a>

### GetLocationRequest
GetLocationRequest - запрос операции GetLocation.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| location_id | [string](#string) |  |  |






<a name="psim-api-v1-GetLocationResponse"></a>

### GetLocationResponse
GetLocationResponse - ответ операции GetLocation.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| location | [psim.catalog.v1.Location](#psim-catalog-v1-Location) |  |  |






<a name="psim-api-v1-GetMaintenanceWindowRequest"></a>

### GetMaintenanceWindowRequest
GetMaintenanceWindowRequest определяет интервал.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| maintenance_id | [string](#string) |  |  |






<a name="psim-api-v1-GetMaintenanceWindowResponse"></a>

### GetMaintenanceWindowResponse
GetMaintenanceWindowResponse включает текущую версию.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| maintenance_window | [psim.catalog.v1.MaintenanceWindow](#psim-catalog-v1-MaintenanceWindow) |  |  |






<a name="psim-api-v1-ImportJob"></a>

### ImportJob
ImportJob - массовый импорт и его результаты.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| import_id | [string](#string) |  |  |
| state | [ImportState](#psim-api-v1-ImportState) |  |  |
| rows_total | [uint32](#uint32) |  |  |
| rows_created | [uint32](#uint32) |  |  |
| rows_updated | [uint32](#uint32) |  |  |
| rows_failed | [uint32](#uint32) |  |  |
| errors | [ImportRowError](#psim-api-v1-ImportRowError) | repeated |  |
| started_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| finished_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-api-v1-ImportRowError"></a>

### ImportRowError
ImportRowError - ошибка одной импортируемой строки.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| row | [uint32](#uint32) |  |  |
| code | [string](#string) |  |  |
| message | [string](#string) |  |  |






<a name="psim-api-v1-ListConnectorsRequest"></a>

### ListConnectorsRequest
ListConnectorsRequest - запрос операции ListConnectors.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListConnectorsResponse"></a>

### ListConnectorsResponse
ListConnectorsResponse - ответ операции ListConnectors.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| connectors | [ConnectorStatus](#psim-api-v1-ConnectorStatus) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListDeviceTypesRequest"></a>

### ListDeviceTypesRequest
ListDeviceTypesRequest - запрос операции ListDeviceTypes.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListDeviceTypesResponse"></a>

### ListDeviceTypesResponse
ListDeviceTypesResponse - ответ операции ListDeviceTypes.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| device_types | [psim.catalog.v1.DeviceType](#psim-catalog-v1-DeviceType) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListDevicesRequest"></a>

### ListDevicesRequest
ListDevicesRequest - запрос операции ListDevices.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| location_id | [string](#string) |  | Любая локация; возвращаются устройства всех вложенных зон. |
| connector_id | [string](#string) |  |  |
| device_type_id | [string](#string) |  |  |
| state | [psim.catalog.v1.ResourceState](#psim-catalog-v1-ResourceState) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListDevicesResponse"></a>

### ListDevicesResponse
ListDevicesResponse - ответ операции ListDevices.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| devices | [psim.catalog.v1.Device](#psim-catalog-v1-Device) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListFloorPlanVersionsRequest"></a>

### ListFloorPlanVersionsRequest
ListFloorPlanVersionsRequest определяет этаж и страницу.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| location_id | [string](#string) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListFloorPlanVersionsResponse"></a>

### ListFloorPlanVersionsResponse
ListFloorPlanVersionsResponse содержит неизменяемые метаданные изображений.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| files | [psim.catalog.v1.StoredFile](#psim-catalog-v1-StoredFile) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListLocationsRequest"></a>

### ListLocationsRequest
ListLocationsRequest - запрос операции ListLocations.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| parent_id | [string](#string) |  |  |
| kind | [psim.catalog.v1.LocationKind](#psim-catalog-v1-LocationKind) |  |  |
| include_archived | [bool](#bool) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListLocationsResponse"></a>

### ListLocationsResponse
ListLocationsResponse - ответ операции ListLocations.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| locations | [psim.catalog.v1.Location](#psim-catalog-v1-Location) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListMaintenanceWindowsRequest"></a>

### ListMaintenanceWindowsRequest
ListMaintenanceWindowsRequest выбирает интервалы и использует курсорную пагинацию.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| site_id | [string](#string) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListMaintenanceWindowsResponse"></a>

### ListMaintenanceWindowsResponse
ListMaintenanceWindowsResponse - список интервалов в доступной области.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| maintenance_windows | [psim.catalog.v1.MaintenanceWindow](#psim-catalog-v1-MaintenanceWindow) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-RegisterConnectorRequest"></a>

### RegisterConnectorRequest
RegisterConnectorRequest - запрос операции RegisterConnector.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| connector | [psim.catalog.v1.Connector](#psim-catalog-v1-Connector) |  |  |
| certificate_pem | [string](#string) |  | Клиентский сертификат без закрытого ключа; шлюз проверяет отпечаток и срок действия. |






<a name="psim-api-v1-RegisterConnectorResponse"></a>

### RegisterConnectorResponse
RegisterConnectorResponse - ответ операции RegisterConnector.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| connector | [psim.catalog.v1.Connector](#psim-catalog-v1-Connector) |  |  |






<a name="psim-api-v1-RevokeConnectorRequest"></a>

### RevokeConnectorRequest
RevokeConnectorRequest - запрос операции RevokeConnector.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| connector_id | [string](#string) |  |  |
| reason | [string](#string) |  |  |
| fingerprint | [string](#string) |  | Пустое значение отключает коннектор; иначе отзывается только выбранный отпечаток. |






<a name="psim-api-v1-RevokeConnectorResponse"></a>

### RevokeConnectorResponse
RevokeConnectorResponse - ответ операции RevokeConnector.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| connector | [psim.catalog.v1.Connector](#psim-catalog-v1-Connector) |  |  |






<a name="psim-api-v1-RotateConnectorCertificateRequest"></a>

### RotateConnectorCertificateRequest
RotateConnectorCertificateRequest - запрос операции RotateConnectorCertificate.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| connector_id | [string](#string) |  |  |
| new_certificate | [psim.catalog.v1.CertificateFingerprint](#psim-catalog-v1-CertificateFingerprint) |  |  |
| previous_not_after | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  | Срок истечения прежнего сертификата. |
| certificate_pem | [string](#string) |  |  |






<a name="psim-api-v1-RotateConnectorCertificateResponse"></a>

### RotateConnectorCertificateResponse
RotateConnectorCertificateResponse - ответ операции RotateConnectorCertificate.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| connector | [psim.catalog.v1.Connector](#psim-catalog-v1-Connector) |  |  |






<a name="psim-api-v1-StartImportRequest"></a>

### StartImportRequest
StartImportRequest - запрос операции StartImport; тело содержит файл.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| format | [string](#string) |  | &quot;csv&quot; или &quot;json&quot;; передаётся как параметр запроса. |
| dry_run | [bool](#bool) |  | Только проверка, без применения изменений. |
| file | [google.api.HttpBody](#google-api-HttpBody) |  |  |






<a name="psim-api-v1-StartImportResponse"></a>

### StartImportResponse
StartImportResponse - ответ операции StartImport.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| import_job | [ImportJob](#psim-api-v1-ImportJob) |  |  |






<a name="psim-api-v1-UpdateDeviceRequest"></a>

### UpdateDeviceRequest
UpdateDeviceRequest - запрос операции UpdateDevice.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| device_id | [string](#string) |  |  |
| device | [psim.catalog.v1.Device](#psim-catalog-v1-Device) |  |  |






<a name="psim-api-v1-UpdateDeviceResponse"></a>

### UpdateDeviceResponse
UpdateDeviceResponse - ответ операции UpdateDevice.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| device | [psim.catalog.v1.Device](#psim-catalog-v1-Device) |  |  |






<a name="psim-api-v1-UpdateDeviceTypeRequest"></a>

### UpdateDeviceTypeRequest
UpdateDeviceTypeRequest - запрос операции UpdateDeviceType.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| device_type_id | [string](#string) |  |  |
| device_type | [psim.catalog.v1.DeviceType](#psim-catalog-v1-DeviceType) |  |  |






<a name="psim-api-v1-UpdateDeviceTypeResponse"></a>

### UpdateDeviceTypeResponse
UpdateDeviceTypeResponse - ответ операции UpdateDeviceType.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| device_type | [psim.catalog.v1.DeviceType](#psim-catalog-v1-DeviceType) |  |  |






<a name="psim-api-v1-UpdateLocationRequest"></a>

### UpdateLocationRequest
UpdateLocationRequest - запрос операции UpdateLocation.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| location_id | [string](#string) |  |  |
| location | [psim.catalog.v1.Location](#psim-catalog-v1-Location) |  |  |






<a name="psim-api-v1-UpdateLocationResponse"></a>

### UpdateLocationResponse
UpdateLocationResponse - ответ операции UpdateLocation.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| location | [psim.catalog.v1.Location](#psim-catalog-v1-Location) |  |  |






<a name="psim-api-v1-UpdateMaintenanceWindowRequest"></a>

### UpdateMaintenanceWindowRequest
UpdateMaintenanceWindowRequest содержит идентификатор из пути и заменяющее значение.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| maintenance_id | [string](#string) |  |  |
| maintenance_window | [psim.catalog.v1.MaintenanceWindow](#psim-catalog-v1-MaintenanceWindow) |  |  |






<a name="psim-api-v1-UpdateMaintenanceWindowResponse"></a>

### UpdateMaintenanceWindowResponse
UpdateMaintenanceWindowResponse включает назначенную версию.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| maintenance_window | [psim.catalog.v1.MaintenanceWindow](#psim-catalog-v1-MaintenanceWindow) |  |  |






<a name="psim-api-v1-UploadFloorPlanRequest"></a>

### UploadFloorPlanRequest
UploadFloorPlanRequest - запрос операции UploadFloorPlan; тело содержит изображение.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| location_id | [string](#string) |  |  |
| width | [double](#double) |  | Размер плана в его единицах; передаётся параметрами запроса. |
| height | [double](#double) |  |  |
| image | [google.api.HttpBody](#google-api-HttpBody) |  |  |






<a name="psim-api-v1-UploadFloorPlanResponse"></a>

### UploadFloorPlanResponse
UploadFloorPlanResponse - ответ операции UploadFloorPlan.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| location | [psim.catalog.v1.Location](#psim-catalog-v1-Location) |  |  |








<a name="psim-api-v1-ImportState"></a>

### ImportState
ImportState - состояние задания импорта.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| IMPORT_STATE_UNSPECIFIED | 0 |  |
| IMPORT_STATE_RUNNING | 1 |  |
| IMPORT_STATE_COMPLETED | 2 |  |
| IMPORT_STATE_FAILED | 3 |  |







<a name="psim-api-v1-CatalogService"></a>

### CatalogService
CatalogService управляет площадками, зданиями, этажами, зонами, типами устройств, устройствами и коннекторами.
Владелец: Resource Catalog.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| GetDeviceType | [GetDeviceTypeRequest](#psim-api-v1-GetDeviceTypeRequest) | [GetDeviceTypeResponse](#psim-api-v1-GetDeviceTypeResponse) | GetDeviceType возвращает один тип устройства. |
| GetConnector | [GetConnectorRequest](#psim-api-v1-GetConnectorRequest) | [GetConnectorResponse](#psim-api-v1-GetConnectorResponse) | GetConnector возвращает последнее состояние регистрации из проекции. |
| ListMaintenanceWindows | [ListMaintenanceWindowsRequest](#psim-api-v1-ListMaintenanceWindowsRequest) | [ListMaintenanceWindowsResponse](#psim-api-v1-ListMaintenanceWindowsResponse) | ListMaintenanceWindows возвращает интервалы в доступной области. |
| GetMaintenanceWindow | [GetMaintenanceWindowRequest](#psim-api-v1-GetMaintenanceWindowRequest) | [GetMaintenanceWindowResponse](#psim-api-v1-GetMaintenanceWindowResponse) | GetMaintenanceWindow возвращает один интервал, включая завершённый. |
| CreateMaintenanceWindow | [CreateMaintenanceWindowRequest](#psim-api-v1-CreateMaintenanceWindowRequest) | [CreateMaintenanceWindowResponse](#psim-api-v1-CreateMaintenanceWindowResponse) | CreateMaintenanceWindow планирует однократный интервал. |
| UpdateMaintenanceWindow | [UpdateMaintenanceWindowRequest](#psim-api-v1-UpdateMaintenanceWindowRequest) | [UpdateMaintenanceWindowResponse](#psim-api-v1-UpdateMaintenanceWindowResponse) | UpdateMaintenanceWindow заменяет запланированный или активный интервал; требуется If-Match. |
| CancelMaintenanceWindow | [CancelMaintenanceWindowRequest](#psim-api-v1-CancelMaintenanceWindowRequest) | [CancelMaintenanceWindowResponse](#psim-api-v1-CancelMaintenanceWindowResponse) | CancelMaintenanceWindow явно завершает интервал; требуется If-Match. |
| ListFloorPlanVersions | [ListFloorPlanVersionsRequest](#psim-api-v1-ListFloorPlanVersionsRequest) | [ListFloorPlanVersionsResponse](#psim-api-v1-ListFloorPlanVersionsResponse) | ListFloorPlanVersions возвращает неизменяемые метаданные предыдущих изображений этажа. |
| ListLocations | [ListLocationsRequest](#psim-api-v1-ListLocationsRequest) | [ListLocationsResponse](#psim-api-v1-ListLocationsResponse) | ListLocations возвращает локации с фильтрацией по родителю или виду. |
| GetLocation | [GetLocationRequest](#psim-api-v1-GetLocationRequest) | [GetLocationResponse](#psim-api-v1-GetLocationResponse) | GetLocation возвращает одну локацию. |
| CreateLocation | [CreateLocationRequest](#psim-api-v1-CreateLocationRequest) | [CreateLocationResponse](#psim-api-v1-CreateLocationResponse) | CreateLocation создаёт площадку, здание, этаж или зону. |
| UpdateLocation | [UpdateLocationRequest](#psim-api-v1-UpdateLocationRequest) | [UpdateLocationResponse](#psim-api-v1-UpdateLocationResponse) | UpdateLocation заменяет изменяемые поля локации; требуется If-Match. |
| ArchiveLocation | [ArchiveLocationRequest](#psim-api-v1-ArchiveLocationRequest) | [ArchiveLocationResponse](#psim-api-v1-ArchiveLocationResponse) | ArchiveLocation архивирует локацию без дочерних локаций и устройств (L3). |
| UploadFloorPlan | [UploadFloorPlanRequest](#psim-api-v1-UploadFloorPlanRequest) | [UploadFloorPlanResponse](#psim-api-v1-UploadFloorPlanResponse) | UploadFloorPlan загружает изображение поэтажного плана. |
| GetFloorPlan | [GetFloorPlanRequest](#psim-api-v1-GetFloorPlanRequest) | [GetFloorPlanResponse](#psim-api-v1-GetFloorPlanResponse) | GetFloorPlan скачивает изображение поэтажного плана. |
| ListDeviceTypes | [ListDeviceTypesRequest](#psim-api-v1-ListDeviceTypesRequest) | [ListDeviceTypesResponse](#psim-api-v1-ListDeviceTypesResponse) | ListDeviceTypes возвращает типы устройств. |
| CreateDeviceType | [CreateDeviceTypeRequest](#psim-api-v1-CreateDeviceTypeRequest) | [CreateDeviceTypeResponse](#psim-api-v1-CreateDeviceTypeResponse) | CreateDeviceType создаёт тип устройства. |
| UpdateDeviceType | [UpdateDeviceTypeRequest](#psim-api-v1-UpdateDeviceTypeRequest) | [UpdateDeviceTypeResponse](#psim-api-v1-UpdateDeviceTypeResponse) | UpdateDeviceType заменяет тип устройства; требуется If-Match. |
| ListDevices | [ListDevicesRequest](#psim-api-v1-ListDevicesRequest) | [ListDevicesResponse](#psim-api-v1-ListDevicesResponse) | ListDevices возвращает устройства с фильтрацией по локации, коннектору или состоянию. |
| GetDevice | [GetDeviceRequest](#psim-api-v1-GetDeviceRequest) | [GetDeviceResponse](#psim-api-v1-GetDeviceResponse) | GetDevice возвращает одно устройство. |
| CreateDevice | [CreateDeviceRequest](#psim-api-v1-CreateDeviceRequest) | [CreateDeviceResponse](#psim-api-v1-CreateDeviceResponse) | CreateDevice регистрирует устройство вместе с его источниками. |
| UpdateDevice | [UpdateDeviceRequest](#psim-api-v1-UpdateDeviceRequest) | [UpdateDeviceResponse](#psim-api-v1-UpdateDeviceResponse) | UpdateDevice заменяет устройство, включая зону, положение и состояние; требуется If-Match. |
| StartImport | [StartImportRequest](#psim-api-v1-StartImportRequest) | [StartImportResponse](#psim-api-v1-StartImportResponse) | StartImport запускает массовый импорт устройств из CSV или JSON. |
| GetImport | [GetImportRequest](#psim-api-v1-GetImportRequest) | [GetImportResponse](#psim-api-v1-GetImportResponse) | GetImport возвращает ход импорта и построчные результаты. |
| ExportDevices | [ExportDevicesRequest](#psim-api-v1-ExportDevicesRequest) | [ExportDevicesResponse](#psim-api-v1-ExportDevicesResponse) | ExportDevices экспортирует устройства в CSV или JSON. |
| ListConnectors | [ListConnectorsRequest](#psim-api-v1-ListConnectorsRequest) | [ListConnectorsResponse](#psim-api-v1-ListConnectorsResponse) | ListConnectors возвращает зарегистрированные коннекторы и их состояния. |
| RegisterConnector | [RegisterConnectorRequest](#psim-api-v1-RegisterConnectorRequest) | [RegisterConnectorResponse](#psim-api-v1-RegisterConnectorResponse) | RegisterConnector регистрирует коннектор и привязывает его сертификат. |
| RotateConnectorCertificate | [RotateConnectorCertificateRequest](#psim-api-v1-RotateConnectorCertificateRequest) | [RotateConnectorCertificateResponse](#psim-api-v1-RotateConnectorCertificateResponse) | RotateConnectorCertificate добавляет новый сертификат и задаёт срок истечения прежнего (C1). |
| RevokeConnector | [RevokeConnectorRequest](#psim-api-v1-RevokeConnectorRequest) | [RevokeConnectorResponse](#psim-api-v1-RevokeConnectorResponse) | RevokeConnector отзывает коннектор и закрывает его сессии (C2). |





<a name="psim_response_v1_command-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/response/v1/command.proto



<a name="psim-response-v1-Command"></a>

### Command
Command - запрос действия на устройстве.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |
| device_id | [string](#string) |  |  |
| connector_id | [string](#string) |  |  |
| capability | [string](#string) |  |  |
| parameters | [Command.ParametersEntry](#psim-response-v1-Command-ParametersEntry) | repeated |  |
| incident_id | [string](#string) |  |  |
| run_id | [string](#string) |  |  |
| step_id | [string](#string) |  |  |
| critical | [bool](#bool) |  |  |
| state | [CommandState](#psim-response-v1-CommandState) |  |  |
| requested_by | [psim.common.v1.Actor](#psim-common-v1-Actor) |  |  |
| approved_by | [psim.common.v1.Actor](#psim-common-v1-Actor) |  |  |
| requested_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| deadline_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| finished_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| result_code | [string](#string) |  | Код ошибки из каталога для неуспешных, отклонённых, просроченных или отменённых команд. |
| result_message | [string](#string) |  |  |
| version | [uint64](#uint64) |  |  |






<a name="psim-response-v1-Command-ParametersEntry"></a>

### Command.ParametersEntry



| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-response-v1-CommandDelivery"></a>

### CommandDelivery
CommandDelivery - команда, направленная в сессию коннектора.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |
| connector_id | [string](#string) |  |  |
| device_external_id | [string](#string) |  |  |
| capability | [string](#string) |  |  |
| parameters | [CommandDelivery.ParametersEntry](#psim-response-v1-CommandDelivery-ParametersEntry) | repeated |  |
| deadline_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| session_id | [string](#string) |  | Доставлять только в эту сессию; пустое значение сохраняет совместимость с прежними производителями. |






<a name="psim-response-v1-CommandDelivery-ParametersEntry"></a>

### CommandDelivery.ParametersEntry



| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-response-v1-CommandDeliveryRecord"></a>

### CommandDeliveryRecord
CommandDeliveryRecord - значение топика psim.commands.v1 с ключом connector_id.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| command_delivery | [CommandDelivery](#psim-response-v1-CommandDelivery) |  |  |






<a name="psim-response-v1-CommandOutcome"></a>

### CommandOutcome
CommandOutcome сообщает конечный результат от коннектора или шлюза.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |
| success | [bool](#bool) |  |  |
| code | [string](#string) |  |  |
| message | [string](#string) |  |  |






<a name="psim-response-v1-CommandReceipt"></a>

### CommandReceipt
CommandReceipt сообщает, что коннектор получил команду.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |






<a name="psim-response-v1-CommandResultRecord"></a>

### CommandResultRecord
CommandResultRecord - значение топика psim.commands.results.v1 с ключом command_id.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| command_receipt | [CommandReceipt](#psim-response-v1-CommandReceipt) |  |  |
| command_outcome | [CommandOutcome](#psim-response-v1-CommandOutcome) |  |  |
| session_id | [string](#string) |  | Сессия, получившая команду. |








<a name="psim-response-v1-CommandState"></a>

### CommandState
CommandState - состояние жизненного цикла команды.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| COMMAND_STATE_UNSPECIFIED | 0 |  |
| COMMAND_STATE_REQUESTED | 1 |  |
| COMMAND_STATE_AWAITING_APPROVAL | 2 |  |
| COMMAND_STATE_SENT | 3 |  |
| COMMAND_STATE_ACKNOWLEDGED | 4 |  |
| COMMAND_STATE_EXECUTED | 5 |  |
| COMMAND_STATE_FAILED | 6 |  |
| COMMAND_STATE_REJECTED | 7 |  |
| COMMAND_STATE_TIMED_OUT | 8 |  |
| COMMAND_STATE_CANCELLED | 9 |  |
| COMMAND_STATE_UNKNOWN | 10 | Выбранная сессия коннектора завершилась без конечного результата; автоматический повтор запрещён. |










<a name="psim_api_v1_commands-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/api/v1/commands.proto



<a name="psim-api-v1-ApproveCommandRequest"></a>

### ApproveCommandRequest
ApproveCommandRequest - запрос операции ApproveCommand.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |
| preview_fingerprint | [string](#string) |  | Необязательная привязка к предварительной проверке этой команды и текущим версиям каталога. |






<a name="psim-api-v1-ApproveCommandResponse"></a>

### ApproveCommandResponse
ApproveCommandResponse - ответ операции ApproveCommand.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command | [psim.response.v1.Command](#psim-response-v1-Command) |  |  |






<a name="psim-api-v1-CancelCommandRequest"></a>

### CancelCommandRequest
CancelCommandRequest - запрос операции CancelCommand.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |






<a name="psim-api-v1-CancelCommandResponse"></a>

### CancelCommandResponse
CancelCommandResponse - ответ операции CancelCommand.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command | [psim.response.v1.Command](#psim-response-v1-Command) |  |  |






<a name="psim-api-v1-GetCommandRequest"></a>

### GetCommandRequest
GetCommandRequest - запрос операции GetCommand.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |






<a name="psim-api-v1-GetCommandResponse"></a>

### GetCommandResponse
GetCommandResponse - ответ операции GetCommand.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command | [psim.response.v1.Command](#psim-response-v1-Command) |  |  |






<a name="psim-api-v1-ListCommandsRequest"></a>

### ListCommandsRequest
ListCommandsRequest - запрос операции ListCommands.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| device_id | [string](#string) |  |  |
| states | [psim.response.v1.CommandState](#psim-response-v1-CommandState) | repeated |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListCommandsResponse"></a>

### ListCommandsResponse
ListCommandsResponse - ответ операции ListCommands.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| commands | [psim.response.v1.Command](#psim-response-v1-Command) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-PreviewCommandRequest"></a>

### PreviewCommandRequest
PreviewCommandRequest использует точный предлагаемый запрос без воздействия на рабочую систему.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command | [RequestCommandRequest](#psim-api-v1-RequestCommandRequest) |  |  |






<a name="psim-api-v1-PreviewCommandResponse"></a>

### PreviewCommandResponse
PreviewCommandResponse определяет настроенное последствие и текущие версии ресурсов.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| device_id | [string](#string) |  |  |
| capability | [string](#string) |  |  |
| consequence | [string](#string) |  |  |
| consequence_unknown | [bool](#bool) |  |  |
| requires_second_supervisor | [bool](#bool) |  |  |
| device_version | [uint64](#uint64) |  |  |
| device_type_version | [uint64](#uint64) |  |  |
| fingerprint | [string](#string) |  |  |






<a name="psim-api-v1-RejectCommandRequest"></a>

### RejectCommandRequest
RejectCommandRequest - запрос операции RejectCommand.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |
| reason | [string](#string) |  |  |






<a name="psim-api-v1-RejectCommandResponse"></a>

### RejectCommandResponse
RejectCommandResponse - ответ операции RejectCommand.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command | [psim.response.v1.Command](#psim-response-v1-Command) |  |  |






<a name="psim-api-v1-RequestCommandRequest"></a>

### RequestCommandRequest
RequestCommandRequest - запрос операции RequestCommand.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |
| device_id | [string](#string) |  |  |
| capability | [string](#string) |  |  |
| parameters | [RequestCommandRequest.ParametersEntry](#psim-api-v1-RequestCommandRequest-ParametersEntry) | repeated |  |
| incident_id | [string](#string) |  |  |
| run_id | [string](#string) |  |  |
| step_id | [string](#string) |  |  |
| preview_fingerprint | [string](#string) |  | Необязательная привязка к предварительной проверке; изменение каталога отклоняет предлагаемый запрос. |






<a name="psim-api-v1-RequestCommandRequest-ParametersEntry"></a>

### RequestCommandRequest.ParametersEntry



| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-api-v1-RequestCommandResponse"></a>

### RequestCommandResponse
RequestCommandResponse - ответ операции RequestCommand.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command | [psim.response.v1.Command](#psim-response-v1-Command) |  |  |












<a name="psim-api-v1-CommandService"></a>

### CommandService
CommandService управляет командами устройств. Владелец: Command Service.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| PreviewCommand | [PreviewCommandRequest](#psim-api-v1-PreviewCommandRequest) | [PreviewCommandResponse](#psim-api-v1-PreviewCommandResponse) | PreviewCommand проверяет область доступа, параметры и capability устройства без отправки команды. |
| RequestCommand | [RequestCommandRequest](#psim-api-v1-RequestCommandRequest) | [RequestCommandResponse](#psim-api-v1-RequestCommandResponse) | RequestCommand запрашивает команду; заголовок Idempotency-Key равен command_id. |
| GetCommand | [GetCommandRequest](#psim-api-v1-GetCommandRequest) | [GetCommandResponse](#psim-api-v1-GetCommandResponse) | GetCommand возвращает одну команду. |
| ListCommands | [ListCommandsRequest](#psim-api-v1-ListCommandsRequest) | [ListCommandsResponse](#psim-api-v1-ListCommandsResponse) | ListCommands возвращает команды по инциденту, устройству или состоянию. |
| ApproveCommand | [ApproveCommandRequest](#psim-api-v1-ApproveCommandRequest) | [ApproveCommandResponse](#psim-api-v1-ApproveCommandResponse) | ApproveCommand одобряет критичную команду; подтверждающий должен отличаться от инициатора (CM3). |
| RejectCommand | [RejectCommandRequest](#psim-api-v1-RejectCommandRequest) | [RejectCommandResponse](#psim-api-v1-RejectCommandResponse) | RejectCommand отклоняет критичную команду. |
| CancelCommand | [CancelCommandRequest](#psim-api-v1-CancelCommandRequest) | [CancelCommandResponse](#psim-api-v1-CancelCommandResponse) | CancelCommand отменяет команду до её отправки. |





<a name="psim_processing_v1_event-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/processing/v1/event.proto



<a name="psim-processing-v1-Event"></a>

### Event
Event - нормализованное событие. envelope.message_id равен исходному event_id,
envelope.sequence равен порядковому номеру источника.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| type | [string](#string) |  | Код типа таксономии, например &quot;intrusion.zone.alarm&quot;. |
| taxonomy_version | [string](#string) |  | Версия таксономии для нормализации, например &quot;1&quot;. |
| event_class | [psim.common.v1.EventClass](#psim-common-v1-EventClass) |  |  |
| severity | [psim.common.v1.Severity](#psim-common-v1-Severity) |  |  |
| site_id | [string](#string) |  |  |
| zone_id | [string](#string) |  |  |
| device_id | [string](#string) |  |  |
| source_id | [string](#string) |  |  |
| connector_id | [string](#string) |  |  |
| source_epoch | [uint32](#uint32) |  |  |
| raw_code | [string](#string) |  | Код события производителя, сохраняется всегда. |
| attributes | [Event.AttributesEntry](#psim-processing-v1-Event-AttributesEntry) | repeated | Стандартные атрибуты таксономии и атрибуты коннектора с префиксом &quot;x.&quot;. |
| significant | [bool](#bool) |  | Значимые события хранятся длительно в Event History (ADR-028). |
| clock_skew_corrected | [bool](#bool) |  | Время события скорректировано из-за рассогласования часов источника (ADR-010). |
| device_type_id | [string](#string) |  |  |
| gateway_session_id | [string](#string) |  | Аутентифицированная сессия приёма; исторические события никогда не повышают актуальность состояния оборудования. |
| live_observation | [bool](#bool) |  |  |
| state_observed_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  | Присутствует только для более нового и свежего наблюдения из сессии приёма. |






<a name="psim-processing-v1-Event-AttributesEntry"></a>

### Event.AttributesEntry



| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-processing-v1-EventRecord"></a>

### EventRecord
EventRecord - значение топика psim.events.normalized.v1 с ключом site_id:zone_id.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| event | [Event](#psim-processing-v1-Event) |  |  |















<a name="psim_api_v1_events-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/api/v1/events.proto



<a name="psim-api-v1-GetEventRequest"></a>

### GetEventRequest
GetEventRequest - запрос операции GetEvent.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| event_id | [string](#string) |  |  |
| from | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  | Необязательный явный интервал; по умолчанию охватывает последние 90 суток. |
| to | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-api-v1-GetEventResponse"></a>

### GetEventResponse
GetEventResponse - ответ операции GetEvent.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| event | [StoredEvent](#psim-api-v1-StoredEvent) |  |  |






<a name="psim-api-v1-ListIncidentEventsRequest"></a>

### ListIncidentEventsRequest
ListIncidentEventsRequest - запрос операции ListIncidentEvents.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |
| from | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| to | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-api-v1-ListIncidentEventsResponse"></a>

### ListIncidentEventsResponse
ListIncidentEventsResponse - ответ операции ListIncidentEvents.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| events | [StoredEvent](#psim-api-v1-StoredEvent) | repeated |  |
| next_page_token | [string](#string) |  |  |
| unavailable_event_ids | [string](#string) | repeated | Ссылки, для которых содержимое события ещё не поступило или уже истекло; содержимое не выдумывается. |






<a name="psim-api-v1-RuleStatistic"></a>

### RuleStatistic
RuleStatistic учитывает каждый детерминированный идентификатор сигнала один раз.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| rule_id | [string](#string) |  |  |
| rule_version | [uint32](#uint32) |  |  |
| signal_count | [uint64](#uint64) |  |  |
| event_reference_count | [uint64](#uint64) |  |  |
| first_fired_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| last_fired_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-api-v1-RuleStatisticsRequest"></a>

### RuleStatisticsRequest
RuleStatisticsRequest выбирает версии правил в ограниченном интервале.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| from | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| to | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| site_ids | [string](#string) | repeated |  |
| zone_ids | [string](#string) | repeated |  |
| rule_ids | [string](#string) | repeated |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-RuleStatisticsResponse"></a>

### RuleStatisticsResponse
RuleStatisticsResponse группирует срабатывания по неизменяемой версии правила.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| statistics | [RuleStatistic](#psim-api-v1-RuleStatistic) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-SearchEventsRequest"></a>

### SearchEventsRequest
SearchEventsRequest - запрос операции SearchEvents.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| from | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| to | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| site_ids | [string](#string) | repeated |  |
| zone_ids | [string](#string) | repeated |  |
| device_ids | [string](#string) | repeated |  |
| types | [string](#string) | repeated | Коды таксономии или префиксы, заканчивающиеся на &quot;.*&quot;. |
| min_severity | [psim.common.v1.Severity](#psim-common-v1-Severity) |  |  |
| significant_only | [bool](#bool) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-SearchEventsResponse"></a>

### SearchEventsResponse
SearchEventsResponse - ответ операции SearchEvents.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| events | [StoredEvent](#psim-api-v1-StoredEvent) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-StoredEvent"></a>

### StoredEvent
StoredEvent - нормализованное событие со временем из его конверта.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| event_id | [string](#string) |  |  |
| occurred_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| received_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| source_seq | [uint64](#uint64) |  |  |
| event | [psim.processing.v1.Event](#psim-processing-v1-Event) |  |  |












<a name="psim-api-v1-EventHistoryService"></a>

### EventHistoryService
EventHistoryService ищет сохранённые события (ADR-028). Владелец: Event History.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| SearchEvents | [SearchEventsRequest](#psim-api-v1-SearchEventsRequest) | [SearchEventsResponse](#psim-api-v1-SearchEventsResponse) | SearchEvents ищет события по локации, устройству, типу и времени, начиная с самых новых. |
| RuleStatistics | [RuleStatisticsRequest](#psim-api-v1-RuleStatisticsRequest) | [RuleStatisticsResponse](#psim-api-v1-RuleStatisticsResponse) | RuleStatistics агрегирует срабатывания в пределах тенанта, области доступа и интервала вызывающего субъекта. |
| GetEvent | [GetEventRequest](#psim-api-v1-GetEventRequest) | [GetEventResponse](#psim-api-v1-GetEventResponse) | GetEvent возвращает одно событие. |
| ListIncidentEvents | [ListIncidentEventsRequest](#psim-api-v1-ListIncidentEventsRequest) | [ListIncidentEventsResponse](#psim-api-v1-ListIncidentEventsResponse) | ListIncidentEvents возвращает исходные события всех сигналов инцидента. |





<a name="psim_api_v1_problem-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/api/v1/problem.proto



<a name="psim-api-v1-FieldViolation"></a>

### FieldViolation
FieldViolation описывает одно некорректное поле.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| field | [string](#string) |  | Путь поля в запросе, например &quot;steps[2].next_step_id&quot;. |
| code | [string](#string) |  |  |
| message | [string](#string) |  |  |






<a name="psim-api-v1-Problem"></a>

### Problem
Problem - тело ошибки каждого неуспешного REST-запроса (RFC 9457).


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| type | [string](#string) |  | URI типа ошибки: https://psim.local/errors/&lt;code&gt;. |
| title | [string](#string) |  |  |
| status | [int32](#int32) |  |  |
| detail | [string](#string) |  |  |
| instance | [string](#string) |  |  |
| code | [string](#string) |  | Код ошибки из contracts/errors/errors.yaml. |
| trace_id | [string](#string) |  |  |
| errors | [FieldViolation](#psim-api-v1-FieldViolation) | repeated |  |















<a name="psim_response_v1_plan-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/response/v1/plan.proto



<a name="psim-response-v1-AutomatedParams"></a>

### AutomatedParams
AutomatedParams вызывает внешний обработчик (ADR-025).


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| webhook_url | [string](#string) |  |  |
| timeout | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |






<a name="psim-response-v1-ChecklistItem"></a>

### ChecklistItem
ChecklistItem - один пункт чек-листа.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| item_id | [string](#string) |  |  |
| text | [string](#string) |  |  |
| required | [bool](#bool) |  |  |






<a name="psim-response-v1-ChecklistParams"></a>

### ChecklistParams
ChecklistParams - список пунктов для проверки.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| items | [ChecklistItem](#psim-response-v1-ChecklistItem) | repeated |  |






<a name="psim-response-v1-CommandParams"></a>

### CommandParams
CommandParams отправляет команду устройствам (P3).


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| capability | [string](#string) |  |  |
| device_id | [string](#string) |  |  |
| target_expression | [string](#string) |  | Выражение цели, например &quot;zone.devices(type == &#x27;access.door&#x27;)&quot;. |
| parameters | [CommandParams.ParametersEntry](#psim-response-v1-CommandParams-ParametersEntry) | repeated |  |
| automatic | [bool](#bool) |  | Исполнять без подтверждения оператора. |






<a name="psim-response-v1-CommandParams-ParametersEntry"></a>

### CommandParams.ParametersEntry



| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-response-v1-DecisionOption"></a>

### DecisionOption
DecisionOption - одна ветка решения.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| option_id | [string](#string) |  |  |
| label | [string](#string) |  |  |
| next_step_id | [string](#string) |  |  |
| suggested_resolution | [string](#string) |  | Предлагаемый результат решения, если этот вариант завершает сценарий, например ложная тревога. |






<a name="psim-response-v1-DecisionParams"></a>

### DecisionParams
DecisionParams выбирает ветку сценария по выбору оператора (P2).


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| question | [string](#string) |  |  |
| options | [DecisionOption](#psim-response-v1-DecisionOption) | repeated |  |






<a name="psim-response-v1-EscalateParams"></a>

### EscalateParams
EscalateParams повышает эскалацию инцидента до указанного уровня.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| level | [uint32](#uint32) |  |  |






<a name="psim-response-v1-EscalationLevel"></a>

### EscalationLevel
EscalationLevel - один уровень эскалации.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| level | [uint32](#uint32) |  |  |
| delay | [google.protobuf.Duration](#google-protobuf-Duration) |  | Задержка после нарушения SLA. |
| recipients | [Recipient](#psim-response-v1-Recipient) | repeated |  |
| channels | [NotificationChannel](#psim-response-v1-NotificationChannel) | repeated |  |






<a name="psim-response-v1-EscalationPolicy"></a>

### EscalationPolicy
EscalationPolicy определяет SLA и уровни эскалации.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| policy_id | [string](#string) |  |  |
| name | [string](#string) |  |  |
| sla | [SlaTarget](#psim-response-v1-SlaTarget) | repeated |  |
| levels | [EscalationLevel](#psim-response-v1-EscalationLevel) | repeated | Строго возрастающие задержки (EP1). |
| version | [uint64](#uint64) |  |  |






<a name="psim-response-v1-InstructionParams"></a>

### InstructionParams
InstructionParams показывает текст, который оператор должен прочитать и подтвердить.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| text | [string](#string) |  |  |






<a name="psim-response-v1-NotifyParams"></a>

### NotifyParams
NotifyParams отправляет уведомления.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| recipients | [Recipient](#psim-response-v1-Recipient) | repeated |  |
| channels | [NotificationChannel](#psim-response-v1-NotificationChannel) | repeated |  |
| template | [string](#string) |  |  |






<a name="psim-response-v1-PlanBinding"></a>

### PlanBinding
PlanBinding выбирает инциденты для сценария; применяется наиболее специфичная привязка (P5).


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_type_id | [string](#string) |  |  |
| site_ids | [string](#string) | repeated |  |
| zone_ids | [string](#string) | repeated |  |
| priorities | [psim.common.v1.Priority](#psim-common-v1-Priority) | repeated |  |
| binding_priority | [int32](#int32) |  | Приоритет при равной специфичности привязок. |






<a name="psim-response-v1-Recipient"></a>

### Recipient
Recipient - получатель уведомления: пользователь или роль в области инцидента.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| user_id | [string](#string) |  |  |
| role | [string](#string) |  |  |
| current_duty | [bool](#bool) |  | Определять текущего дежурного площадки при отправке эскалации. Должно быть true. |






<a name="psim-response-v1-ResponsePlan"></a>

### ResponsePlan
ResponsePlan - одна неизменяемая версия сценария реагирования.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| plan_id | [string](#string) |  |  |
| version | [uint32](#uint32) |  |  |
| name | [string](#string) |  |  |
| state | [PlanState](#psim-response-v1-PlanState) |  |  |
| bindings | [PlanBinding](#psim-response-v1-PlanBinding) | repeated |  |
| start_step_id | [string](#string) |  |  |
| steps | [Step](#psim-response-v1-Step) | repeated |  |
| published_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| published_by | [psim.common.v1.Actor](#psim-common-v1-Actor) |  |  |






<a name="psim-response-v1-SlaTarget"></a>

### SlaTarget
SlaTarget - SLA для одного приоритета.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| priority | [psim.common.v1.Priority](#psim-common-v1-Priority) |  |  |
| acknowledge_within | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| resolve_within | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |






<a name="psim-response-v1-Step"></a>

### Step
Step - узел графа сценария.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| step_id | [string](#string) |  |  |
| title | [string](#string) |  |  |
| mandatory | [bool](#bool) |  |  |
| next_step_id | [string](#string) |  | Следующий шаг для всех видов, кроме decision; пустой для последнего шага. |
| instruction | [InstructionParams](#psim-response-v1-InstructionParams) |  |  |
| checklist | [ChecklistParams](#psim-response-v1-ChecklistParams) |  |  |
| decision | [DecisionParams](#psim-response-v1-DecisionParams) |  |  |
| command | [CommandParams](#psim-response-v1-CommandParams) |  |  |
| notify | [NotifyParams](#psim-response-v1-NotifyParams) |  |  |
| wait | [WaitParams](#psim-response-v1-WaitParams) |  |  |
| escalate | [EscalateParams](#psim-response-v1-EscalateParams) |  |  |
| automated | [AutomatedParams](#psim-response-v1-AutomatedParams) |  |  |






<a name="psim-response-v1-WaitParams"></a>

### WaitParams
WaitParams приостанавливает сценарий.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| duration | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |








<a name="psim-response-v1-NotificationChannel"></a>

### NotificationChannel
NotificationChannel - канал доставки.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| NOTIFICATION_CHANNEL_UNSPECIFIED | 0 |  |
| NOTIFICATION_CHANNEL_IN_APP | 1 |  |
| NOTIFICATION_CHANNEL_EMAIL | 2 |  |
| NOTIFICATION_CHANNEL_WEBHOOK | 3 |  |



<a name="psim-response-v1-PlanState"></a>

### PlanState
PlanState - состояние жизненного цикла версии сценария.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| PLAN_STATE_UNSPECIFIED | 0 |  |
| PLAN_STATE_DRAFT | 1 |  |
| PLAN_STATE_PUBLISHED | 2 |  |
| PLAN_STATE_ARCHIVED | 3 |  |










<a name="psim_response_v1_run-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/response/v1/run.proto



<a name="psim-response-v1-ResponseRun"></a>

### ResponseRun
ResponseRun - исполнение версии сценария для инцидента.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| run_id | [string](#string) |  |  |
| incident_id | [string](#string) |  |  |
| plan_id | [string](#string) |  |  |
| plan_version | [uint32](#uint32) |  |  |
| state | [RunState](#psim-response-v1-RunState) |  |  |
| steps | [StepExecution](#psim-response-v1-StepExecution) | repeated |  |
| active_step_ids | [string](#string) | repeated |  |
| blocking_step_ids | [string](#string) | repeated | Незавершённые обязательные шаги (RR5). |
| started_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| finished_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| version | [uint64](#uint64) |  |  |






<a name="psim-response-v1-StepExecution"></a>

### StepExecution
StepExecution - состояние исполнения одного шага.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| step_id | [string](#string) |  |  |
| state | [StepState](#psim-response-v1-StepState) |  |  |
| attempt | [uint32](#uint32) |  |  |
| actor | [psim.common.v1.Actor](#psim-common-v1-Actor) |  |  |
| activated_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| finished_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| result | [StepResult](#psim-response-v1-StepResult) |  |  |






<a name="psim-response-v1-StepResult"></a>

### StepResult
StepResult - результат шага.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| checked_item_ids | [string](#string) | repeated |  |
| decision_option_id | [string](#string) |  |  |
| command_ids | [string](#string) | repeated |  |
| note | [string](#string) |  | Причина пропуска или override либо свободное примечание. |
| overridden | [bool](#bool) |  | Задаётся, когда supervisor завершает неуспешный шаг через Override. |








<a name="psim-response-v1-RunState"></a>

### RunState
RunState - состояние жизненного цикла запуска.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| RUN_STATE_UNSPECIFIED | 0 |  |
| RUN_STATE_RUNNING | 1 |  |
| RUN_STATE_COMPLETED | 2 |  |
| RUN_STATE_CANCELLED | 3 |  |



<a name="psim-response-v1-StepState"></a>

### StepState
StepState - состояние шага.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| STEP_STATE_UNSPECIFIED | 0 |  |
| STEP_STATE_PENDING | 1 |  |
| STEP_STATE_ACTIVE | 2 |  |
| STEP_STATE_COMPLETED | 3 |  |
| STEP_STATE_SKIPPED | 4 |  |
| STEP_STATE_FAILED | 5 |  |
| STEP_STATE_CANCELLED | 6 |  |










<a name="psim_api_v1_response-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/api/v1/response.proto



<a name="psim-api-v1-ArchivePlanRequest"></a>

### ArchivePlanRequest
ArchivePlanRequest - запрос операции ArchivePlan.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| plan_id | [string](#string) |  |  |






<a name="psim-api-v1-ArchivePlanResponse"></a>

### ArchivePlanResponse
ArchivePlanResponse - ответ операции ArchivePlan.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| plan | [psim.response.v1.ResponsePlan](#psim-response-v1-ResponsePlan) |  |  |






<a name="psim-api-v1-CompleteStepRequest"></a>

### CompleteStepRequest
CompleteStepRequest - запрос операции CompleteStep.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| run_id | [string](#string) |  |  |
| step_id | [string](#string) |  |  |
| result | [psim.response.v1.StepResult](#psim-response-v1-StepResult) |  |  |






<a name="psim-api-v1-CompleteStepResponse"></a>

### CompleteStepResponse
CompleteStepResponse - ответ операции CompleteStep.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| run | [psim.response.v1.ResponseRun](#psim-response-v1-ResponseRun) |  |  |






<a name="psim-api-v1-CreateEscalationPolicyRequest"></a>

### CreateEscalationPolicyRequest
CreateEscalationPolicyRequest - запрос операции CreateEscalationPolicy.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| policy | [psim.response.v1.EscalationPolicy](#psim-response-v1-EscalationPolicy) |  |  |






<a name="psim-api-v1-CreateEscalationPolicyResponse"></a>

### CreateEscalationPolicyResponse
CreateEscalationPolicyResponse - ответ операции CreateEscalationPolicy.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| policy | [psim.response.v1.EscalationPolicy](#psim-response-v1-EscalationPolicy) |  |  |






<a name="psim-api-v1-GetIncidentRunRequest"></a>

### GetIncidentRunRequest
GetIncidentRunRequest - запрос операции GetIncidentRun.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |






<a name="psim-api-v1-GetIncidentRunResponse"></a>

### GetIncidentRunResponse
GetIncidentRunResponse - ответ операции GetIncidentRun.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| run | [psim.response.v1.ResponseRun](#psim-response-v1-ResponseRun) |  |  |
| plan | [psim.response.v1.ResponsePlan](#psim-response-v1-ResponsePlan) |  | Версия сценария, используемая запуском. |






<a name="psim-api-v1-GetPlanRequest"></a>

### GetPlanRequest
GetPlanRequest - запрос операции GetPlan.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| plan_id | [string](#string) |  |  |
| version | [uint32](#uint32) |  |  |






<a name="psim-api-v1-GetPlanResponse"></a>

### GetPlanResponse
GetPlanResponse - ответ операции GetPlan.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| plan | [psim.response.v1.ResponsePlan](#psim-response-v1-ResponsePlan) |  |  |






<a name="psim-api-v1-GetSiteDutyRequest"></a>

### GetSiteDutyRequest
GetSiteDutyRequest выбирает площадку.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| site_id | [string](#string) |  |  |






<a name="psim-api-v1-GetSiteDutyResponse"></a>

### GetSiteDutyResponse
GetSiteDutyResponse содержит сведения об ответственном.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| duty | [SiteDuty](#psim-api-v1-SiteDuty) |  |  |






<a name="psim-api-v1-ListEscalationPoliciesRequest"></a>

### ListEscalationPoliciesRequest
ListEscalationPoliciesRequest - запрос операции ListEscalationPolicies.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListEscalationPoliciesResponse"></a>

### ListEscalationPoliciesResponse
ListEscalationPoliciesResponse - ответ операции ListEscalationPolicies.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| policies | [psim.response.v1.EscalationPolicy](#psim-response-v1-EscalationPolicy) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListPlansRequest"></a>

### ListPlansRequest
ListPlansRequest - запрос операции ListPlans.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_type_id | [string](#string) |  |  |
| include_archived | [bool](#bool) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListPlansResponse"></a>

### ListPlansResponse
ListPlansResponse - ответ операции ListPlans.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| plans | [psim.response.v1.ResponsePlan](#psim-response-v1-ResponsePlan) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-OverrideStepRequest"></a>

### OverrideStepRequest
OverrideStepRequest - запрос операции OverrideStep.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| run_id | [string](#string) |  |  |
| step_id | [string](#string) |  |  |
| reason | [string](#string) |  |  |






<a name="psim-api-v1-OverrideStepResponse"></a>

### OverrideStepResponse
OverrideStepResponse - ответ операции OverrideStep.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| run | [psim.response.v1.ResponseRun](#psim-response-v1-ResponseRun) |  |  |






<a name="psim-api-v1-PublishPlanRequest"></a>

### PublishPlanRequest
PublishPlanRequest - запрос операции PublishPlan.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| plan_id | [string](#string) |  |  |
| version | [uint32](#uint32) |  |  |






<a name="psim-api-v1-PublishPlanResponse"></a>

### PublishPlanResponse
PublishPlanResponse - ответ операции PublishPlan.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| plan | [psim.response.v1.ResponsePlan](#psim-response-v1-ResponsePlan) |  |  |






<a name="psim-api-v1-RetryStepRequest"></a>

### RetryStepRequest
RetryStepRequest - запрос операции RetryStep.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| run_id | [string](#string) |  |  |
| step_id | [string](#string) |  |  |






<a name="psim-api-v1-RetryStepResponse"></a>

### RetryStepResponse
RetryStepResponse - ответ операции RetryStep.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| run | [psim.response.v1.ResponseRun](#psim-response-v1-ResponseRun) |  |  |






<a name="psim-api-v1-SavePlanDraftRequest"></a>

### SavePlanDraftRequest
SavePlanDraftRequest - запрос операции SavePlanDraft.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| plan | [psim.response.v1.ResponsePlan](#psim-response-v1-ResponsePlan) |  |  |






<a name="psim-api-v1-SavePlanDraftResponse"></a>

### SavePlanDraftResponse
SavePlanDraftResponse - ответ операции SavePlanDraft.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| plan | [psim.response.v1.ResponsePlan](#psim-response-v1-ResponsePlan) |  |  |






<a name="psim-api-v1-SetSiteDutyRequest"></a>

### SetSiteDutyRequest
SetSiteDutyRequest заменяет ответственного.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| site_id | [string](#string) |  |  |
| duty | [SiteDuty](#psim-api-v1-SiteDuty) |  |  |






<a name="psim-api-v1-SetSiteDutyResponse"></a>

### SetSiteDutyResponse
SetSiteDutyResponse содержит сохранённую версию.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| duty | [SiteDuty](#psim-api-v1-SiteDuty) |  |  |






<a name="psim-api-v1-SiteDuty"></a>

### SiteDuty
SiteDuty задаёт явного ответственного; пустой user_id активирует резервного получателя.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| site_id | [string](#string) |  |  |
| user_id | [string](#string) |  |  |
| fallback_user_id | [string](#string) |  |  |
| version | [uint64](#uint64) |  |  |






<a name="psim-api-v1-SkipStepRequest"></a>

### SkipStepRequest
SkipStepRequest - запрос операции SkipStep.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| run_id | [string](#string) |  |  |
| step_id | [string](#string) |  |  |
| reason | [string](#string) |  |  |






<a name="psim-api-v1-SkipStepResponse"></a>

### SkipStepResponse
SkipStepResponse - ответ операции SkipStep.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| run | [psim.response.v1.ResponseRun](#psim-response-v1-ResponseRun) |  |  |






<a name="psim-api-v1-UpdateEscalationPolicyRequest"></a>

### UpdateEscalationPolicyRequest
UpdateEscalationPolicyRequest - запрос операции UpdateEscalationPolicy.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| policy_id | [string](#string) |  |  |
| policy | [psim.response.v1.EscalationPolicy](#psim-response-v1-EscalationPolicy) |  |  |






<a name="psim-api-v1-UpdateEscalationPolicyResponse"></a>

### UpdateEscalationPolicyResponse
UpdateEscalationPolicyResponse - ответ операции UpdateEscalationPolicy.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| policy | [psim.response.v1.EscalationPolicy](#psim-response-v1-EscalationPolicy) |  |  |






<a name="psim-api-v1-ValidatePlanRequest"></a>

### ValidatePlanRequest
ValidatePlanRequest - запрос операции ValidatePlan.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| plan | [psim.response.v1.ResponsePlan](#psim-response-v1-ResponsePlan) |  |  |






<a name="psim-api-v1-ValidatePlanResponse"></a>

### ValidatePlanResponse
ValidatePlanResponse - ответ операции ValidatePlan.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| valid | [bool](#bool) |  |  |
| violations | [FieldViolation](#psim-api-v1-FieldViolation) | repeated |  |












<a name="psim-api-v1-EscalationPolicyService"></a>

### EscalationPolicyService
EscalationPolicyService управляет SLA и политиками эскалации. Владелец: Response Engine.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| ListEscalationPolicies | [ListEscalationPoliciesRequest](#psim-api-v1-ListEscalationPoliciesRequest) | [ListEscalationPoliciesResponse](#psim-api-v1-ListEscalationPoliciesResponse) | ListEscalationPolicies возвращает политики эскалации. |
| CreateEscalationPolicy | [CreateEscalationPolicyRequest](#psim-api-v1-CreateEscalationPolicyRequest) | [CreateEscalationPolicyResponse](#psim-api-v1-CreateEscalationPolicyResponse) | CreateEscalationPolicy создаёт политику. |
| UpdateEscalationPolicy | [UpdateEscalationPolicyRequest](#psim-api-v1-UpdateEscalationPolicyRequest) | [UpdateEscalationPolicyResponse](#psim-api-v1-UpdateEscalationPolicyResponse) | UpdateEscalationPolicy заменяет политику; требуется If-Match. |


<a name="psim-api-v1-ResponsePlanService"></a>

### ResponsePlanService
ResponsePlanService управляет сценариями реагирования. Владелец: Response Engine.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| ListPlans | [ListPlansRequest](#psim-api-v1-ListPlansRequest) | [ListPlansResponse](#psim-api-v1-ListPlansResponse) | ListPlans возвращает последнюю версию каждого сценария. |
| GetPlan | [GetPlanRequest](#psim-api-v1-GetPlanRequest) | [GetPlanResponse](#psim-api-v1-GetPlanResponse) | GetPlan возвращает версию сценария; при версии 0 выбирается последняя. |
| SavePlanDraft | [SavePlanDraftRequest](#psim-api-v1-SavePlanDraftRequest) | [SavePlanDraftResponse](#psim-api-v1-SavePlanDraftResponse) | SavePlanDraft создаёт сценарий или его новую черновую версию. |
| ValidatePlan | [ValidatePlanRequest](#psim-api-v1-ValidatePlanRequest) | [ValidatePlanResponse](#psim-api-v1-ValidatePlanResponse) | ValidatePlan проверяет сценарий без сохранения (P1-P5). |
| PublishPlan | [PublishPlanRequest](#psim-api-v1-PublishPlanRequest) | [PublishPlanResponse](#psim-api-v1-PublishPlanResponse) | PublishPlan публикует черновую версию; опубликованные версии неизменяемы. |
| ArchivePlan | [ArchivePlanRequest](#psim-api-v1-ArchivePlanRequest) | [ArchivePlanResponse](#psim-api-v1-ArchivePlanResponse) | ArchivePlan архивирует сценарий. |


<a name="psim-api-v1-ResponseRunService"></a>

### ResponseRunService
ResponseRunService исполняет шаги сценариев. Владелец: Response Engine.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| GetIncidentRun | [GetIncidentRunRequest](#psim-api-v1-GetIncidentRunRequest) | [GetIncidentRunResponse](#psim-api-v1-GetIncidentRunResponse) | GetIncidentRun возвращает активный или последний запуск сценария инцидента. |
| CompleteStep | [CompleteStepRequest](#psim-api-v1-CompleteStepRequest) | [CompleteStepResponse](#psim-api-v1-CompleteStepResponse) | CompleteStep завершает активный шаг с указанным результатом. |
| SkipStep | [SkipStepRequest](#psim-api-v1-SkipStepRequest) | [SkipStepResponse](#psim-api-v1-SkipStepResponse) | SkipStep пропускает необязательный шаг с указанием причины (RR3). |
| RetryStep | [RetryStepRequest](#psim-api-v1-RetryStepRequest) | [RetryStepResponse](#psim-api-v1-RetryStepResponse) | RetryStep повторяет неуспешный шаг. |
| OverrideStep | [OverrideStepRequest](#psim-api-v1-OverrideStepRequest) | [OverrideStepResponse](#psim-api-v1-OverrideStepResponse) | OverrideStep завершает неуспешный шаг по решению supervisor. |


<a name="psim-api-v1-SiteDutyService"></a>

### SiteDutyService
SiteDutyService назначает явного ответственного за площадку. Владелец: Response Engine.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| GetSiteDuty | [GetSiteDutyRequest](#psim-api-v1-GetSiteDutyRequest) | [GetSiteDutyResponse](#psim-api-v1-GetSiteDutyResponse) | GetSiteDuty возвращает текущего и резервного получателей. |
| SetSiteDuty | [SetSiteDutyRequest](#psim-api-v1-SetSiteDutyRequest) | [SetSiteDutyResponse](#psim-api-v1-SetSiteDutyResponse) | SetSiteDuty заменяет ответственного с оптимистической проверкой конкурентных изменений. |





<a name="psim_incident_v1_incident-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/incident/v1/incident.proto



<a name="psim-incident-v1-AlarmControl"></a>

### AlarmControl
AlarmControl хранит независимое управление видимостью с ограниченным сроком.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| reason | [string](#string) |  |  |
| actor_id | [string](#string) |  |  |
| until | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-incident-v1-Incident"></a>

### Incident
Incident группирует сигналы и отслеживает жизненный цикл реагирования (ADR-027).


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| incident_type_id | [string](#string) |  |  |
| site_id | [string](#string) |  |  |
| zone_id | [string](#string) |  |  |
| state | [IncidentState](#psim-incident-v1-IncidentState) |  |  |
| priority | [psim.common.v1.Priority](#psim-common-v1-Priority) |  |  |
| priority_source | [PrioritySource](#psim-incident-v1-PrioritySource) |  |  |
| grouping_key | [string](#string) |  |  |
| group_until | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| signal_ids | [string](#string) | repeated | Ограниченная выборка прикреплённых идентификаторов; ListIncidentSignals возвращает полный список. |
| title | [string](#string) |  |  |
| assignee_id | [string](#string) |  | Идентификатор ответственного оператора. |
| escalation_level | [uint32](#uint32) |  | 0 означает отсутствие эскалации. |
| blocking_steps | [StepRef](#psim-incident-v1-StepRef) | repeated | Незавершённые обязательные шаги активного запуска сценария (I7). |
| response_run_version | [uint64](#uint64) |  | Версия запуска сценария, применённая к blocking_steps. |
| resolution | [Resolution](#psim-incident-v1-Resolution) |  |  |
| resolution_note | [string](#string) |  |  |
| related_incident_id | [string](#string) |  | Предыдущий инцидент с тем же ключом группировки, если есть (I4). |
| created_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| acknowledged_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| resolved_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| closed_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| version | [uint64](#uint64) |  | Версия агрегата, используемая как ETag. |
| incident_type_version | [uint64](#uint64) |  | Конфигурация типа инцидента, зафиксированная при его создании. |
| pending_transfer | [IncidentTransfer](#psim-incident-v1-IncidentTransfer) |  | Ожидающая передача сохраняет текущего ответственного до подтверждения получателем. |
| auto_close_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  | Долговечный срок для автоматического закрытия после решения. |
| signal_count | [uint64](#uint64) |  | Общее количество идентификаторов в коллекции сигналов с пагинацией. |
| signal_ids_truncated | [bool](#bool) |  | Предварительная выборка ограничена 1000 идентификаторов. |
| escalation_policy_id | [string](#string) |  | Идентификатор политики, зафиксированный вместе с версией типа инцидента. |
| operational | [OperationalState](#psim-incident-v1-OperationalState) |  | Независимое состояние тревоги, подтверждения и временное управление видимостью. |






<a name="psim-incident-v1-IncidentTransfer"></a>

### IncidentTransfer
IncidentTransfer - ожидающий запрос, привязанный к версии агрегата.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| transfer_id | [string](#string) |  |  |
| requested_by | [string](#string) |  |  |
| recipient_id | [string](#string) |  |  |
| comment | [string](#string) |  |  |
| requested_version | [uint64](#uint64) |  |  |






<a name="psim-incident-v1-IncidentType"></a>

### IncidentType
IncidentType - тип ситуаций, определяющий сценарий реагирования и SLA.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_type_id | [string](#string) |  |  |
| code | [string](#string) |  |  |
| name | [string](#string) |  |  |
| default_priority | [psim.common.v1.Priority](#psim-common-v1-Priority) |  |  |
| grouping_window | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| escalation_policy_id | [string](#string) |  |  |
| response_required | [bool](#bool) |  | Если задано, инцидент без подходящего сценария реагирования помечается. |
| auto_close_after | [google.protobuf.Duration](#google-protobuf-Duration) |  | Задержка между решением и автоматическим закрытием. |
| version | [uint64](#uint64) |  |  |
| priority_matrix | [IncidentType.PriorityMatrixEntry](#psim-incident-v1-IncidentType-PriorityMatrixEntry) | repeated | Необязательное сопоставление приоритетов с ключом по числовому входному приоритету (1-4). |






<a name="psim-incident-v1-IncidentType-PriorityMatrixEntry"></a>

### IncidentType.PriorityMatrixEntry



| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| key | [int32](#int32) |  |  |
| value | [psim.common.v1.Priority](#psim-common-v1-Priority) |  |  |






<a name="psim-incident-v1-Link"></a>

### Link
Link - внешняя ссылка, прикреплённая к инциденту, например видеоклип или документ.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| link_id | [string](#string) |  |  |
| url | [string](#string) |  |  |
| title | [string](#string) |  |  |






<a name="psim-incident-v1-OperationalState"></a>

### OperationalState
OperationalState сохраняет состояние условия независимо от жизненного цикла инцидента.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| active | [bool](#bool) |  |  |
| acknowledged | [bool](#bool) |  |  |
| acknowledged_by | [string](#string) |  |  |
| condition_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| shelved | [AlarmControl](#psim-incident-v1-AlarmControl) |  |  |
| suppressed | [AlarmControl](#psim-incident-v1-AlarmControl) |  |  |
| presence | [PresenceLease](#psim-incident-v1-PresenceLease) | repeated |  |
| acknowledge_due | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| resolve_due | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| condition_known | [bool](#bool) |  | False для ранее существовавших инцидентов, состояние процесса которых никогда не наблюдалось. |






<a name="psim-incident-v1-PresenceLease"></a>

### PresenceLease
PresenceLease носит информационный характер, никогда не предоставляет права и не блокирует действие с инцидентом.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| actor_id | [string](#string) |  |  |
| session_id | [string](#string) |  |  |
| lease_id | [string](#string) |  |  |
| until | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| editing | [bool](#bool) |  |  |






<a name="psim-incident-v1-StepRef"></a>

### StepRef
StepRef указывает на шаг запуска сценария реагирования.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| run_id | [string](#string) |  |  |
| step_id | [string](#string) |  |  |
| title | [string](#string) |  |  |






<a name="psim-incident-v1-TimelineEntry"></a>

### TimelineEntry
TimelineEntry - одна запись хронологии инцидента.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| entry_id | [string](#string) |  |  |
| incident_id | [string](#string) |  |  |
| at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| actor | [psim.common.v1.Actor](#psim-common-v1-Actor) |  |  |
| kind | [TimelineEntryKind](#psim-incident-v1-TimelineEntryKind) |  |  |
| text | [string](#string) |  |  |
| ref_id | [string](#string) |  | Идентификатор связанного объекта: сигнала, шага, команды, уведомления или ссылки. |
| details | [TimelineEntry.DetailsEntry](#psim-incident-v1-TimelineEntry-DetailsEntry) | repeated |  |






<a name="psim-incident-v1-TimelineEntry-DetailsEntry"></a>

### TimelineEntry.DetailsEntry



| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |








<a name="psim-incident-v1-IncidentState"></a>

### IncidentState
IncidentState - состояние жизненного цикла инцидента.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| INCIDENT_STATE_UNSPECIFIED | 0 |  |
| INCIDENT_STATE_NEW | 1 |  |
| INCIDENT_STATE_ACKNOWLEDGED | 2 |  |
| INCIDENT_STATE_IN_PROGRESS | 3 |  |
| INCIDENT_STATE_RESOLVED | 4 |  |
| INCIDENT_STATE_CLOSED | 5 |  |



<a name="psim-incident-v1-PrioritySource"></a>

### PrioritySource
PrioritySource указывает, вычислен приоритет или задан вручную (I6).

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| PRIORITY_SOURCE_UNSPECIFIED | 0 |  |
| PRIORITY_SOURCE_COMPUTED | 1 |  |
| PRIORITY_SOURCE_MANUAL | 2 |  |



<a name="psim-incident-v1-Resolution"></a>

### Resolution
Resolution - результат решения инцидента.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| RESOLUTION_UNSPECIFIED | 0 |  |
| RESOLUTION_CONFIRMED | 1 |  |
| RESOLUTION_FALSE_ALARM | 2 |  |
| RESOLUTION_TEST | 3 |  |
| RESOLUTION_DUPLICATE | 4 |  |



<a name="psim-incident-v1-TimelineEntryKind"></a>

### TimelineEntryKind
TimelineEntryKind классифицирует записи хронологии.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| TIMELINE_ENTRY_KIND_UNSPECIFIED | 0 |  |
| TIMELINE_ENTRY_KIND_STATE_CHANGED | 1 |  |
| TIMELINE_ENTRY_KIND_SIGNAL_ATTACHED | 2 |  |
| TIMELINE_ENTRY_KIND_PRIORITY_CHANGED | 3 |  |
| TIMELINE_ENTRY_KIND_ASSIGNED | 4 |  |
| TIMELINE_ENTRY_KIND_ESCALATED | 5 |  |
| TIMELINE_ENTRY_KIND_COMMENT | 6 |  |
| TIMELINE_ENTRY_KIND_LINK | 7 |  |
| TIMELINE_ENTRY_KIND_STEP | 8 |  |
| TIMELINE_ENTRY_KIND_COMMAND | 9 |  |
| TIMELINE_ENTRY_KIND_NOTIFICATION | 10 |  |










<a name="psim_api_v1_feed-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/api/v1/feed.proto



<a name="psim-api-v1-ConnectorRuntimeStatus"></a>

### ConnectorRuntimeStatus
ConnectorRuntimeStatus описывает соединение, не утверждая состояние оборудования.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| connector_id | [string](#string) |  |  |
| connection | [string](#string) |  |  |
| quality | [DataQuality](#psim-api-v1-DataQuality) |  |  |
| confirmed_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| session_id | [string](#string) |  |  |
| resource_state | [psim.catalog.v1.ResourceState](#psim-catalog-v1-ResourceState) |  |  |






<a name="psim-api-v1-DeviceStatus"></a>

### DeviceStatus
DeviceStatus - текущее состояние устройства, выведенное из событий alarm, fault, restore и state.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| device_id | [string](#string) |  |  |
| state | [psim.common.v1.EventClass](#psim-common-v1-EventClass) |  |  |
| last_type | [string](#string) |  | Последний тип таксономии, изменяющий состояние. |
| last_changed_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| online | [bool](#bool) |  |  |
| quality | [DataQuality](#psim-api-v1-DataQuality) |  |  |
| confirmed_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| resource_state | [psim.catalog.v1.ResourceState](#psim-catalog-v1-ResourceState) |  |  |
| connector_id | [string](#string) |  |  |
| connection | [string](#string) |  | Доступность оборудования остаётся неизвестной, если наблюдались только события. |
| sources | [SourceRuntimeStatus](#psim-api-v1-SourceRuntimeStatus) | repeated |  |






<a name="psim-api-v1-GetIncidentFeedRequest"></a>

### GetIncidentFeedRequest
GetIncidentFeedRequest - запрос операции GetIncidentFeed.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| site_ids | [string](#string) | repeated |  |
| zone_ids | [string](#string) | repeated |  |






<a name="psim-api-v1-GetIncidentFeedResponse"></a>

### GetIncidentFeedResponse
GetIncidentFeedResponse - ответ операции GetIncidentFeed.
Без пагинации: консоли нужна вся лента, до 10 000 инцидентов.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incidents | [psim.incident.v1.Incident](#psim-incident-v1-Incident) | repeated |  |
| resume_token | [string](#string) |  | Токен для кадра realtime Subscribe. |
| snapshot_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-api-v1-GetLocationStatusRequest"></a>

### GetLocationStatusRequest
GetLocationStatusRequest - запрос операции GetLocationStatus.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| location_id | [string](#string) |  |  |






<a name="psim-api-v1-GetLocationStatusResponse"></a>

### GetLocationStatusResponse
GetLocationStatusResponse - ответ операции GetLocationStatus.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| zones | [ZoneStatus](#psim-api-v1-ZoneStatus) | repeated |  |
| devices | [DeviceStatus](#psim-api-v1-DeviceStatus) | repeated |  |
| resume_token | [string](#string) |  |  |
| snapshot_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| connectors | [ConnectorRuntimeStatus](#psim-api-v1-ConnectorRuntimeStatus) | repeated |  |
| maintenance_windows | [psim.catalog.v1.MaintenanceWindow](#psim-catalog-v1-MaintenanceWindow) | repeated |  |
| site_duties | [SiteDuty](#psim-api-v1-SiteDuty) | repeated |  |






<a name="psim-api-v1-GetOperationalSnapshotRequest"></a>

### GetOperationalSnapshotRequest
GetOperationalSnapshotRequest выбирает историческое представление без подписки realtime.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| location_id | [string](#string) |  |  |
| site_ids | [string](#string) | repeated |  |






<a name="psim-api-v1-GetOperationalSnapshotResponse"></a>

### GetOperationalSnapshotResponse
GetOperationalSnapshotResponse доступен только для чтения и никогда не подтверждает текущую связность.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| feed | [GetIncidentFeedResponse](#psim-api-v1-GetIncidentFeedResponse) |  |  |
| location | [GetLocationStatusResponse](#psim-api-v1-GetLocationStatusResponse) |  |  |
| catalog_locations | [psim.catalog.v1.Location](#psim-catalog-v1-Location) | repeated |  |
| catalog_devices | [psim.catalog.v1.Device](#psim-catalog-v1-Device) | repeated |  |
| historical | [bool](#bool) |  |  |
| available_from | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-api-v1-GetOperatorLoadRequest"></a>

### GetOperatorLoadRequest
GetOperatorLoadRequest - запрос операции GetOperatorLoad.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| site_ids | [string](#string) | repeated |  |






<a name="psim-api-v1-GetOperatorLoadResponse"></a>

### GetOperatorLoadResponse
GetOperatorLoadResponse - ответ операции GetOperatorLoad.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| operators | [OperatorLoad](#psim-api-v1-OperatorLoad) | repeated |  |
| resume_token | [string](#string) |  |  |
| snapshot_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-api-v1-OperatorLoad"></a>

### OperatorLoad
OperatorLoad - текущая загрузка одного оператора.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| user_id | [string](#string) |  |  |
| display_name | [string](#string) |  |  |
| assigned_open | [uint32](#uint32) |  |  |
| escalated | [uint32](#uint32) |  |  |
| duty_site_ids | [string](#string) | repeated |  |
| fallback_site_ids | [string](#string) | repeated |  |






<a name="psim-api-v1-SourceRuntimeStatus"></a>

### SourceRuntimeStatus
SourceRuntimeStatus хранит состояние по событиям, сообщённые значения и доступность оборудования независимо.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| source_id | [string](#string) |  |  |
| state | [psim.common.v1.EventClass](#psim-common-v1-EventClass) |  |  |
| state_quality | [DataQuality](#psim-api-v1-DataQuality) |  |  |
| state_confirmed_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| values | [SourceRuntimeStatus.ValuesEntry](#psim-api-v1-SourceRuntimeStatus-ValuesEntry) | repeated |  |
| values_quality | [DataQuality](#psim-api-v1-DataQuality) |  |  |
| values_confirmed_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| availability | [string](#string) |  |  |
| availability_confirmed_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| availability_quality | [DataQuality](#psim-api-v1-DataQuality) |  |  |






<a name="psim-api-v1-SourceRuntimeStatus-ValuesEntry"></a>

### SourceRuntimeStatus.ValuesEntry



| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-api-v1-ZoneStatus"></a>

### ZoneStatus
ZoneStatus - агрегированное состояние зоны.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| zone_id | [string](#string) |  |  |
| state | [psim.common.v1.EventClass](#psim-common-v1-EventClass) |  | Наивысший класс среди активных состояний устройств. |
| armed | [bool](#bool) |  |  |
| open_incidents | [uint32](#uint32) |  |  |
| quality | [DataQuality](#psim-api-v1-DataQuality) |  |  |
| confirmed_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| resource_state | [psim.catalog.v1.ResourceState](#psim-catalog-v1-ResourceState) |  |  |








<a name="psim-api-v1-DataQuality"></a>

### DataQuality
DataQuality отделяет актуальность наблюдений оборудования от жизненного цикла ресурса.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| DATA_QUALITY_UNSPECIFIED | 0 |  |
| DATA_QUALITY_ACTUAL | 1 |  |
| DATA_QUALITY_STALE | 2 |  |
| DATA_QUALITY_UNKNOWN | 3 |  |







<a name="psim-api-v1-FeedService"></a>

### FeedService
FeedService предоставляет read-модели консоли. Владелец: Projection Service.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| GetOperationalSnapshot | [GetOperationalSnapshotRequest](#psim-api-v1-GetOperationalSnapshotRequest) | [GetOperationalSnapshotResponse](#psim-api-v1-GetOperationalSnapshotResponse) | GetOperationalSnapshot возвращает данные ленты и плана на исторический момент серверного snapshot. |
| GetIncidentFeed | [GetIncidentFeedRequest](#psim-api-v1-GetIncidentFeedRequest) | [GetIncidentFeedResponse](#psim-api-v1-GetIncidentFeedResponse) | GetIncidentFeed возвращает активные инциденты в доступной области и resume token, на котором snapshot согласован (ADR-019). |
| GetLocationStatus | [GetLocationStatusRequest](#psim-api-v1-GetLocationStatusRequest) | [GetLocationStatusResponse](#psim-api-v1-GetLocationStatusResponse) | GetLocationStatus возвращает состояния устройств и зон локации для представления объекта. |
| GetOperatorLoad | [GetOperatorLoadRequest](#psim-api-v1-GetOperatorLoadRequest) | [GetOperatorLoadResponse](#psim-api-v1-GetOperatorLoadResponse) | GetOperatorLoad возвращает текущую загрузку операторов; требуется supervisor. |





<a name="psim_processing_v1_signal-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/processing/v1/signal.proto



<a name="psim-processing-v1-Signal"></a>

### Signal
Signal - результат срабатывания правила корреляции или внешнего производителя (ADR-025).
envelope.message_id - детерминированный signal_id (ADR-005).


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| origin | [SignalOrigin](#psim-processing-v1-SignalOrigin) |  |  |
| correlation_key | [string](#string) |  |  |
| grouping_key | [string](#string) |  | Ключ, по которому Incident Service прикрепляет сигнал к открытому инциденту. |
| incident_type_id | [string](#string) |  |  |
| priority | [psim.common.v1.Priority](#psim-common-v1-Priority) |  |  |
| site_id | [string](#string) |  |  |
| zone_id | [string](#string) |  | Пустое значение для сигналов правил с областью шире зоны. |
| event_ids | [string](#string) | repeated |  |
| first_event_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| last_event_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| summary | [string](#string) |  |  |
| explanation_ref | [string](#string) |  | Необязательная ссылка на объяснение, созданное внешним модулем. |
| absence_deadline | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  | Срабатывание absence по таймеру может не иметь входных событий. Свидетельством служит срок ожидания. |






<a name="psim-processing-v1-SignalOrigin"></a>

### SignalOrigin
SignalOrigin определяет источник сигнала.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| kind | [SignalOriginKind](#psim-processing-v1-SignalOriginKind) |  |  |
| rule_id | [string](#string) |  | Идентификатор правила корреляции для SIGNAL_ORIGIN_KIND_RULE. |
| rule_version | [uint32](#uint32) |  |  |
| producer | [string](#string) |  | Зарегистрированное имя внешнего производителя для SIGNAL_ORIGIN_KIND_EXTERNAL. |






<a name="psim-processing-v1-SignalRecord"></a>

### SignalRecord
SignalRecord - значение топика psim.signals.v1 с ключом grouping_key.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| signal | [Signal](#psim-processing-v1-Signal) |  |  |








<a name="psim-processing-v1-SignalOriginKind"></a>

### SignalOriginKind
SignalOriginKind - вид производителя сигнала.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| SIGNAL_ORIGIN_KIND_UNSPECIFIED | 0 |  |
| SIGNAL_ORIGIN_KIND_RULE | 1 |  |
| SIGNAL_ORIGIN_KIND_EXTERNAL | 2 |  |










<a name="psim_api_v1_incidents-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/api/v1/incidents.proto



<a name="psim-api-v1-AcknowledgeIncidentRequest"></a>

### AcknowledgeIncidentRequest
AcknowledgeIncidentRequest - запрос операции AcknowledgeIncident.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |






<a name="psim-api-v1-AcknowledgeIncidentResponse"></a>

### AcknowledgeIncidentResponse
AcknowledgeIncidentResponse - ответ операции AcknowledgeIncident.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-api-v1-AddIncidentCommentRequest"></a>

### AddIncidentCommentRequest
AddIncidentCommentRequest - запрос операции AddIncidentComment.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| text | [string](#string) |  |  |






<a name="psim-api-v1-AddIncidentCommentResponse"></a>

### AddIncidentCommentResponse
AddIncidentCommentResponse - ответ операции AddIncidentComment.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| entry | [psim.incident.v1.TimelineEntry](#psim-incident-v1-TimelineEntry) |  |  |






<a name="psim-api-v1-AddIncidentLinkRequest"></a>

### AddIncidentLinkRequest
AddIncidentLinkRequest - запрос операции AddIncidentLink.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| url | [string](#string) |  |  |
| title | [string](#string) |  |  |






<a name="psim-api-v1-AddIncidentLinkResponse"></a>

### AddIncidentLinkResponse
AddIncidentLinkResponse - ответ операции AddIncidentLink.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| link | [psim.incident.v1.Link](#psim-incident-v1-Link) |  |  |






<a name="psim-api-v1-AssignIncidentRequest"></a>

### AssignIncidentRequest
AssignIncidentRequest - запрос операции AssignIncident.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| assignee_id | [string](#string) |  |  |
| reason | [string](#string) |  |  |






<a name="psim-api-v1-AssignIncidentResponse"></a>

### AssignIncidentResponse
AssignIncidentResponse - ответ операции AssignIncident.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-api-v1-CloseIncidentRequest"></a>

### CloseIncidentRequest
CloseIncidentRequest - запрос операции CloseIncident.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |






<a name="psim-api-v1-CloseIncidentResponse"></a>

### CloseIncidentResponse
CloseIncidentResponse - ответ операции CloseIncident.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-api-v1-CreateIncidentTypeRequest"></a>

### CreateIncidentTypeRequest
CreateIncidentTypeRequest - запрос операции CreateIncidentType.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_type | [psim.incident.v1.IncidentType](#psim-incident-v1-IncidentType) |  |  |






<a name="psim-api-v1-CreateIncidentTypeResponse"></a>

### CreateIncidentTypeResponse
CreateIncidentTypeResponse - ответ операции CreateIncidentType.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_type | [psim.incident.v1.IncidentType](#psim-incident-v1-IncidentType) |  |  |






<a name="psim-api-v1-GetIncidentRequest"></a>

### GetIncidentRequest
GetIncidentRequest - запрос операции GetIncident.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |






<a name="psim-api-v1-GetIncidentResponse"></a>

### GetIncidentResponse
GetIncidentResponse - ответ операции GetIncident.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-api-v1-IncidentSignal"></a>

### IncidentSignal
IncidentSignal - сигнал, прикреплённый к инциденту.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| signal_id | [string](#string) |  |  |
| signal | [psim.processing.v1.Signal](#psim-processing-v1-Signal) |  |  |
| attached_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-api-v1-ListIncidentSignalsRequest"></a>

### ListIncidentSignalsRequest
ListIncidentSignalsRequest - запрос операции ListIncidentSignals.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListIncidentSignalsResponse"></a>

### ListIncidentSignalsResponse
ListIncidentSignalsResponse - ответ операции ListIncidentSignals.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| signals | [IncidentSignal](#psim-api-v1-IncidentSignal) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListIncidentTypesRequest"></a>

### ListIncidentTypesRequest
ListIncidentTypesRequest - запрос операции ListIncidentTypes.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListIncidentTypesResponse"></a>

### ListIncidentTypesResponse
ListIncidentTypesResponse - ответ операции ListIncidentTypes.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_types | [psim.incident.v1.IncidentType](#psim-incident-v1-IncidentType) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListIncidentsRequest"></a>

### ListIncidentsRequest
ListIncidentsRequest - запрос операции ListIncidents.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| states | [psim.incident.v1.IncidentState](#psim-incident-v1-IncidentState) | repeated |  |
| priorities | [psim.common.v1.Priority](#psim-common-v1-Priority) | repeated |  |
| site_ids | [string](#string) | repeated |  |
| zone_ids | [string](#string) | repeated |  |
| incident_type_ids | [string](#string) | repeated |  |
| assignee_id | [string](#string) |  |  |
| escalated_only | [bool](#bool) |  |  |
| created_from | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| created_to | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListIncidentsResponse"></a>

### ListIncidentsResponse
ListIncidentsResponse - ответ операции ListIncidents.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incidents | [psim.incident.v1.Incident](#psim-incident-v1-Incident) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListTimelineRequest"></a>

### ListTimelineRequest
ListTimelineRequest - запрос операции ListTimeline.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListTimelineResponse"></a>

### ListTimelineResponse
ListTimelineResponse - ответ операции ListTimeline.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| entries | [psim.incident.v1.TimelineEntry](#psim-incident-v1-TimelineEntry) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-ReopenIncidentRequest"></a>

### ReopenIncidentRequest
ReopenIncidentRequest - запрос операции ReopenIncident.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| reason | [string](#string) |  |  |






<a name="psim-api-v1-ReopenIncidentResponse"></a>

### ReopenIncidentResponse
ReopenIncidentResponse - ответ операции ReopenIncident.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-api-v1-ResolveIncidentRequest"></a>

### ResolveIncidentRequest
ResolveIncidentRequest - запрос операции ResolveIncident.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| resolution | [psim.incident.v1.Resolution](#psim-incident-v1-Resolution) |  |  |
| note | [string](#string) |  |  |
| known_response_run_version | [uint64](#uint64) |  | Версия запуска сценария, известная клиенту; защищает от незамеченных обязательных шагов. |






<a name="psim-api-v1-ResolveIncidentResponse"></a>

### ResolveIncidentResponse
ResolveIncidentResponse - ответ операции ResolveIncident.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-api-v1-SetIncidentPriorityRequest"></a>

### SetIncidentPriorityRequest
SetIncidentPriorityRequest - запрос операции SetIncidentPriority.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| priority | [psim.common.v1.Priority](#psim-common-v1-Priority) |  |  |
| reason | [string](#string) |  |  |






<a name="psim-api-v1-SetIncidentPriorityResponse"></a>

### SetIncidentPriorityResponse
SetIncidentPriorityResponse - ответ операции SetIncidentPriority.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-api-v1-StartIncidentWorkRequest"></a>

### StartIncidentWorkRequest
StartIncidentWorkRequest - запрос операции StartIncidentWork.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |






<a name="psim-api-v1-StartIncidentWorkResponse"></a>

### StartIncidentWorkResponse
StartIncidentWorkResponse - ответ операции StartIncidentWork.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-api-v1-TransferIncidentItem"></a>

### TransferIncidentItem
TransferIncidentItem - одна атомарная операция в пакете передачи.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| expected_version | [uint64](#uint64) |  |  |
| recipient_id | [string](#string) |  |  |
| comment | [string](#string) |  |  |
| transfer_id | [string](#string) |  | Непустое значение подтверждает существующую передачу; вызывающий пользователь должен быть её получателем. |






<a name="psim-api-v1-TransferIncidentResult"></a>

### TransferIncidentResult
TransferIncidentResult содержит один инцидент или стабильный код ошибки.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |
| error_code | [string](#string) |  |  |






<a name="psim-api-v1-TransferIncidentsRequest"></a>

### TransferIncidentsRequest
TransferIncidentsRequest ограничивает размер пакета и не обещает семантику «всё или ничего».


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| items | [TransferIncidentItem](#psim-api-v1-TransferIncidentItem) | repeated |  |






<a name="psim-api-v1-TransferIncidentsResponse"></a>

### TransferIncidentsResponse
TransferIncidentsResponse сохраняет входной порядок и сообщает результат каждой атомарной операции.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| results | [TransferIncidentResult](#psim-api-v1-TransferIncidentResult) | repeated |  |






<a name="psim-api-v1-UpdateIncidentTypeRequest"></a>

### UpdateIncidentTypeRequest
UpdateIncidentTypeRequest - запрос операции UpdateIncidentType.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_type_id | [string](#string) |  |  |
| incident_type | [psim.incident.v1.IncidentType](#psim-incident-v1-IncidentType) |  |  |






<a name="psim-api-v1-UpdateIncidentTypeResponse"></a>

### UpdateIncidentTypeResponse
UpdateIncidentTypeResponse - ответ операции UpdateIncidentType.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_type | [psim.incident.v1.IncidentType](#psim-incident-v1-IncidentType) |  |  |












<a name="psim-api-v1-IncidentService"></a>

### IncidentService
IncidentService управляет инцидентами и их типами. Владелец: Incident Service.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| ListIncidents | [ListIncidentsRequest](#psim-api-v1-ListIncidentsRequest) | [ListIncidentsResponse](#psim-api-v1-ListIncidentsResponse) | ListIncidents возвращает инциденты в доступной области. |
| GetIncident | [GetIncidentRequest](#psim-api-v1-GetIncidentRequest) | [GetIncidentResponse](#psim-api-v1-GetIncidentResponse) | GetIncident возвращает один инцидент; ETag равен версии инцидента. |
| ListTimeline | [ListTimelineRequest](#psim-api-v1-ListTimelineRequest) | [ListTimelineResponse](#psim-api-v1-ListTimelineResponse) | ListTimeline возвращает хронологию инцидента. |
| ListIncidentSignals | [ListIncidentSignalsRequest](#psim-api-v1-ListIncidentSignalsRequest) | [ListIncidentSignalsResponse](#psim-api-v1-ListIncidentSignalsResponse) | ListIncidentSignals возвращает сигналы, прикреплённые к инциденту. |
| AcknowledgeIncident | [AcknowledgeIncidentRequest](#psim-api-v1-AcknowledgeIncidentRequest) | [AcknowledgeIncidentResponse](#psim-api-v1-AcknowledgeIncidentResponse) | AcknowledgeIncident принимает инцидент; вызывающий пользователь становится ответственным. |
| StartIncidentWork | [StartIncidentWorkRequest](#psim-api-v1-StartIncidentWorkRequest) | [StartIncidentWorkResponse](#psim-api-v1-StartIncidentWorkResponse) | StartIncidentWork переводит инцидент в in_progress. |
| AssignIncident | [AssignIncidentRequest](#psim-api-v1-AssignIncidentRequest) | [AssignIncidentResponse](#psim-api-v1-AssignIncidentResponse) | AssignIncident назначает или переназначает инцидент. |
| SetIncidentPriority | [SetIncidentPriorityRequest](#psim-api-v1-SetIncidentPriorityRequest) | [SetIncidentPriorityResponse](#psim-api-v1-SetIncidentPriorityResponse) | SetIncidentPriority вручную задаёт приоритет; требуется supervisor. |
| AddIncidentComment | [AddIncidentCommentRequest](#psim-api-v1-AddIncidentCommentRequest) | [AddIncidentCommentResponse](#psim-api-v1-AddIncidentCommentResponse) | AddIncidentComment добавляет комментарий в хронологию. |
| AddIncidentLink | [AddIncidentLinkRequest](#psim-api-v1-AddIncidentLinkRequest) | [AddIncidentLinkResponse](#psim-api-v1-AddIncidentLinkResponse) | AddIncidentLink добавляет внешнюю ссылку. |
| ResolveIncident | [ResolveIncidentRequest](#psim-api-v1-ResolveIncidentRequest) | [ResolveIncidentResponse](#psim-api-v1-ResolveIncidentResponse) | ResolveIncident решает инцидент; операция отклоняется, пока остаются незавершённые обязательные шаги (I7). |
| ReopenIncident | [ReopenIncidentRequest](#psim-api-v1-ReopenIncidentRequest) | [ReopenIncidentResponse](#psim-api-v1-ReopenIncidentResponse) | ReopenIncident повторно открывает решённый инцидент (I10). |
| CloseIncident | [CloseIncidentRequest](#psim-api-v1-CloseIncidentRequest) | [CloseIncidentResponse](#psim-api-v1-CloseIncidentResponse) | CloseIncident закрывает решённый инцидент до срабатывания таймера автозакрытия; требуется supervisor. |
| TransferIncidents | [TransferIncidentsRequest](#psim-api-v1-TransferIncidentsRequest) | [TransferIncidentsResponse](#psim-api-v1-TransferIncidentsResponse) | TransferIncidents независимо применяет запросы или подтверждения передачи с проверкой версии. |
| ListIncidentTypes | [ListIncidentTypesRequest](#psim-api-v1-ListIncidentTypesRequest) | [ListIncidentTypesResponse](#psim-api-v1-ListIncidentTypesResponse) | ListIncidentTypes возвращает типы инцидентов. |
| CreateIncidentType | [CreateIncidentTypeRequest](#psim-api-v1-CreateIncidentTypeRequest) | [CreateIncidentTypeResponse](#psim-api-v1-CreateIncidentTypeResponse) | CreateIncidentType создаёт тип инцидента. |
| UpdateIncidentType | [UpdateIncidentTypeRequest](#psim-api-v1-UpdateIncidentTypeRequest) | [UpdateIncidentTypeResponse](#psim-api-v1-UpdateIncidentTypeResponse) | UpdateIncidentType заменяет тип инцидента; требуется If-Match. |





<a name="psim_api_v1_me-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/api/v1/me.proto



<a name="psim-api-v1-GetMeRequest"></a>

### GetMeRequest
GetMeRequest - запрос операции GetMe.






<a name="psim-api-v1-GetMeResponse"></a>

### GetMeResponse
GetMeResponse - ответ операции GetMe.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| user_id | [string](#string) |  |  |
| tenant_id | [string](#string) |  |  |
| display_name | [string](#string) |  |  |
| email | [string](#string) |  |  |
| roles | [string](#string) | repeated | Роли: operator, supervisor, admin, integrator, auditor, manager. |
| site_ids | [string](#string) | repeated |  |
| zone_ids | [string](#string) | repeated |  |
| actions | [string](#string) | repeated | Действия, разрешённые таблицей ролей, для скрытия недоступных действий UI (ADR-020). |
| locale | [string](#string) |  |  |












<a name="psim-api-v1-MeService"></a>

### MeService
MeService описывает вызывающего субъекта. Владелец: API Gateway; данные из токена доступа и таблицы политик.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| GetMe | [GetMeRequest](#psim-api-v1-GetMeRequest) | [GetMeResponse](#psim-api-v1-GetMeResponse) | GetMe возвращает идентичность, роли, области доступа и разрешённые действия вызывающего субъекта. |





<a name="psim_response_v1_notification-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/response/v1/notification.proto



<a name="psim-response-v1-Notification"></a>

### Notification
Notification - сообщение получателю по каналу.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| notification_id | [string](#string) |  |  |
| recipient_user_id | [string](#string) |  |  |
| channel | [NotificationChannel](#psim-response-v1-NotificationChannel) |  |  |
| state | [NotificationState](#psim-response-v1-NotificationState) |  |  |
| template | [string](#string) |  |  |
| data | [Notification.DataEntry](#psim-response-v1-Notification-DataEntry) | repeated |  |
| attempts | [uint32](#uint32) |  |  |
| incident_id | [string](#string) |  |  |
| aggregated_notification_ids | [string](#string) | repeated | Задаётся для агрегированных уведомлений (N3). |
| created_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| delivered_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| read | [bool](#bool) |  | Только для внутренних уведомлений приложения. |
| version | [uint64](#uint64) |  |  |
| request_id | [string](#string) |  | Стабильные идентификаторы исходного запроса и квитанции доставки. |
| receipt_id | [string](#string) |  |  |
| failure_code | [string](#string) |  |  |






<a name="psim-response-v1-Notification-DataEntry"></a>

### Notification.DataEntry



| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |








<a name="psim-response-v1-NotificationState"></a>

### NotificationState
NotificationState - состояние доставки.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| NOTIFICATION_STATE_UNSPECIFIED | 0 |  |
| NOTIFICATION_STATE_PENDING | 1 |  |
| NOTIFICATION_STATE_SENT | 2 |  |
| NOTIFICATION_STATE_DELIVERED | 3 |  |
| NOTIFICATION_STATE_FAILED | 4 |  |
| NOTIFICATION_STATE_AGGREGATED | 5 |  |
| NOTIFICATION_STATE_UNKNOWN | 6 | После сбоя транспорта или процесса нельзя делать вывод о приёме внешней системой. |










<a name="psim_api_v1_notifications-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/api/v1/notifications.proto



<a name="psim-api-v1-ListMyNotificationsRequest"></a>

### ListMyNotificationsRequest
ListMyNotificationsRequest - запрос операции ListMyNotifications.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| unread_only | [bool](#bool) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListMyNotificationsResponse"></a>

### ListMyNotificationsResponse
ListMyNotificationsResponse - ответ операции ListMyNotifications.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| notifications | [psim.response.v1.Notification](#psim-response-v1-Notification) | repeated |  |
| next_page_token | [string](#string) |  |  |
| unread_count | [uint32](#uint32) |  |  |






<a name="psim-api-v1-MarkNotificationReadRequest"></a>

### MarkNotificationReadRequest
MarkNotificationReadRequest - запрос операции MarkNotificationRead.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| notification_id | [string](#string) |  |  |






<a name="psim-api-v1-MarkNotificationReadResponse"></a>

### MarkNotificationReadResponse
MarkNotificationReadResponse - ответ операции MarkNotificationRead.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| notification | [psim.response.v1.Notification](#psim-response-v1-Notification) |  |  |






<a name="psim-api-v1-NotificationContact"></a>

### NotificationContact
NotificationContact задаёт адреса доставки; роли и области доступа учётной записи поступают от провайдера идентификации.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| user_id | [string](#string) |  |  |
| site_id | [string](#string) |  |  |
| email | [string](#string) |  |  |
| webhook_url | [string](#string) |  |  |
| version | [uint64](#uint64) |  |  |






<a name="psim-api-v1-ResolveUnknownDeliveryRequest"></a>

### ResolveUnknownDeliveryRequest
ResolveUnknownDeliveryRequest фиксирует свидетельства от получателя транспорта.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| notification_id | [string](#string) |  |  |
| accepted | [bool](#bool) |  |  |
| receipt_id | [string](#string) |  |  |






<a name="psim-api-v1-ResolveUnknownDeliveryResponse"></a>

### ResolveUnknownDeliveryResponse
ResolveUnknownDeliveryResponse содержит уведомление с согласованным исходом.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| notification | [psim.response.v1.Notification](#psim-response-v1-Notification) |  |  |






<a name="psim-api-v1-SetNotificationContactRequest"></a>

### SetNotificationContactRequest
SetNotificationContactRequest заменяет адреса доставки.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| user_id | [string](#string) |  |  |
| contact | [NotificationContact](#psim-api-v1-NotificationContact) |  |  |






<a name="psim-api-v1-SetNotificationContactResponse"></a>

### SetNotificationContactResponse
SetNotificationContactResponse содержит сохранённые адреса доставки.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| contact | [NotificationContact](#psim-api-v1-NotificationContact) |  |  |












<a name="psim-api-v1-NotificationContactService"></a>

### NotificationContactService
NotificationContactService управляет адресами доставки каналов. Владелец: Notification Service.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| SetNotificationContact | [SetNotificationContactRequest](#psim-api-v1-SetNotificationContactRequest) | [SetNotificationContactResponse](#psim-api-v1-SetNotificationContactResponse) | SetNotificationContact заменяет адреса доставки пользователя на площадке. |
| ResolveUnknownDelivery | [ResolveUnknownDeliveryRequest](#psim-api-v1-ResolveUnknownDeliveryRequest) | [ResolveUnknownDeliveryResponse](#psim-api-v1-ResolveUnknownDeliveryResponse) | ResolveUnknownDelivery фиксирует проверенный исход транспорта; никогда не отправляет уведомление повторно. |


<a name="psim-api-v1-NotificationService"></a>

### NotificationService
NotificationService предоставляет внутренние уведомления вызывающего пользователя. Владелец: Notification Service.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| ListMyNotifications | [ListMyNotificationsRequest](#psim-api-v1-ListMyNotificationsRequest) | [ListMyNotificationsResponse](#psim-api-v1-ListMyNotificationsResponse) | ListMyNotifications возвращает внутренние уведомления пользователя, начиная с самых новых. |
| MarkNotificationRead | [MarkNotificationReadRequest](#psim-api-v1-MarkNotificationReadRequest) | [MarkNotificationReadResponse](#psim-api-v1-MarkNotificationReadResponse) | MarkNotificationRead отмечает внутреннее уведомление как прочитанное. |





<a name="psim_api_v1_operations-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/api/v1/operations.proto



<a name="psim-api-v1-ActExerciseRequest"></a>

### ActExerciseRequest
ActExerciseRequest принимает acknowledge, start-work, command или resolve в порядке учебного сценария.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| exercise_id | [string](#string) |  |  |
| action | [string](#string) |  |  |
| reason | [string](#string) |  |  |






<a name="psim-api-v1-ActExerciseResponse"></a>

### ActExerciseResponse
ActExerciseResponse возвращает версионированную оценку и состояние симуляции.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| exercise | [ExerciseState](#psim-api-v1-ExerciseState) |  |  |






<a name="psim-api-v1-AskAssistantRequest"></a>

### AskAssistantRequest
AskAssistantRequest никогда не принимает содержимое источников, URL моделей или исполняемые действия.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| question | [string](#string) |  |  |






<a name="psim-api-v1-AskAssistantResponse"></a>

### AskAssistantResponse
AskAssistantResponse сохраняет доступные свидетельства при недоступности настроенного провайдера.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| request_id | [string](#string) |  |  |
| provider | [string](#string) |  |  |
| model | [string](#string) |  |  |
| sources | [OperationSource](#psim-api-v1-OperationSource) | repeated |  |
| claims | [AssistantClaim](#psim-api-v1-AssistantClaim) | repeated |  |
| unavailable | [bool](#bool) |  |  |






<a name="psim-api-v1-AssistantClaim"></a>

### AssistantClaim
AssistantClaim - недоверенное предложение модели, связанное с серверным источником.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| text | [string](#string) |  |  |
| source_id | [string](#string) |  |  |
| assumption | [bool](#bool) |  |  |






<a name="psim-api-v1-CreateExerciseRequest"></a>

### CreateExerciseRequest
CreateExerciseRequest копирует один доступный инцидент в изолированную сессию с ограниченным сроком жизни.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| title | [string](#string) |  |  |






<a name="psim-api-v1-CreateExerciseResponse"></a>

### CreateExerciseResponse
CreateExerciseResponse возвращает частную учебную сессию.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| exercise | [ExerciseState](#psim-api-v1-ExerciseState) |  |  |






<a name="psim-api-v1-ExerciseState"></a>

### ExerciseState
ExerciseState нельзя отправить как рабочую команду или запуск сценария реагирования.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| exercise_id | [string](#string) |  |  |
| owner_id | [string](#string) |  |  |
| version | [uint64](#uint64) |  |  |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |
| score | [uint32](#uint32) |  |  |
| completed_actions | [string](#string) | repeated |  |
| expires_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-api-v1-GetExerciseRequest"></a>

### GetExerciseRequest
GetExerciseRequest выбирает сессию текущего пользователя.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| exercise_id | [string](#string) |  |  |






<a name="psim-api-v1-GetExerciseResponse"></a>

### GetExerciseResponse
GetExerciseResponse возвращает частную сессию без воздействия на рабочую систему.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| exercise | [ExerciseState](#psim-api-v1-ExerciseState) |  |  |






<a name="psim-api-v1-GetIncidentRecoveryRequest"></a>

### GetIncidentRecoveryRequest
GetIncidentRecoveryRequest сравнивает известную версию с текущим агрегатом.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| known_version | [uint64](#uint64) |  |  |






<a name="psim-api-v1-GetIncidentRecoveryResponse"></a>

### GetIncidentRecoveryResponse
GetIncidentRecoveryResponse ограничивает изменения и явно требует новый snapshot при разрыве истории.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |
| changed_versions | [uint64](#uint64) |  |  |
| snapshot_required | [bool](#bool) |  |  |
| changes | [psim.incident.v1.TimelineEntry](#psim-incident-v1-TimelineEntry) | repeated |  |






<a name="psim-api-v1-GetIncidentSnapshotsRequest"></a>

### GetIncidentSnapshotsRequest
GetIncidentSnapshotsRequest выбирает версии после курсора или последний snapshot на указанный момент.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| after_version | [uint64](#uint64) |  |  |
| page_size | [uint32](#uint32) |  |  |
| at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-api-v1-GetIncidentSnapshotsResponse"></a>

### GetIncidentSnapshotsResponse
GetIncidentSnapshotsResponse никогда не представляет историю как текущую доступность устройств.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| snapshots | [IncidentSnapshot](#psim-api-v1-IncidentSnapshot) | repeated |  |
| next_version | [uint64](#uint64) |  |  |
| historical | [bool](#bool) |  |  |






<a name="psim-api-v1-GetOperationsRequest"></a>

### GetOperationsRequest
GetOperationsRequest выбирает один текущий инцидент.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |






<a name="psim-api-v1-GetOperationsResponse"></a>

### GetOperationsResponse
GetOperationsResponse объединяет текущее состояние с явным риском SLA и происхождением данных.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |
| sources | [OperationSource](#psim-api-v1-OperationSource) | repeated |  |
| sla_risk | [string](#string) |  |  |
| evaluated_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-api-v1-IncidentSnapshot"></a>

### IncidentSnapshot
IncidentSnapshot содержит неизменяемое состояние и время его фиксации сервером.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |
| captured_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-api-v1-OperationSource"></a>

### OperationSource
OperationSource определяет серверный источник свидетельств и его точную версию.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| source_id | [string](#string) |  |  |
| version | [uint64](#uint64) |  |  |
| obtained_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| stale | [bool](#bool) |  |  |
| text | [string](#string) |  |  |






<a name="psim-api-v1-SetPresenceRequest"></a>

### SetPresenceRequest
SetPresenceRequest получает или продлевает аренду сессии; для освобождения нужен текущий lease_id.
Пустой lease_id получает новую аренду. TTL ограничен 60 секундами и сроком JWT.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| session_id | [string](#string) |  |  |
| lease_id | [string](#string) |  |  |
| editing | [bool](#bool) |  |  |
| release | [bool](#bool) |  |  |
| ttl_seconds | [uint32](#uint32) |  |  |






<a name="psim-api-v1-SetPresenceResponse"></a>

### SetPresenceResponse
SetPresenceResponse возвращает версионированное состояние совместной работы.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-api-v1-UpdateAlarmRequest"></a>

### UpdateAlarmRequest
UpdateAlarmRequest принимает acknowledge, activate, return, shelve, unshelve, suppress или unsuppress.
Подавление и изменение условия требуют supervisor. Для управления видимостью нужен срок окончания.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| action | [string](#string) |  |  |
| reason | [string](#string) |  |  |
| until | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-api-v1-UpdateAlarmResponse"></a>

### UpdateAlarmResponse
UpdateAlarmResponse возвращает зафиксированную версию агрегата.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |












<a name="psim-api-v1-OperationalService"></a>

### OperationalService
OperationalService владеет управлением тревогами, совместной работой, историей и изолированными упражнениями.
Владелец: Incident Service. Все исходные данные разрешаются по текущей области доступа вызывающего субъекта.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| GetOperations | [GetOperationsRequest](#psim-api-v1-GetOperationsRequest) | [GetOperationsResponse](#psim-api-v1-GetOperationsResponse) | GetOperations проверяет актуальные права перед доступом к сохранённому состоянию. |
| UpdateAlarm | [UpdateAlarmRequest](#psim-api-v1-UpdateAlarmRequest) | [UpdateAlarmResponse](#psim-api-v1-UpdateAlarmResponse) | UpdateAlarm проверяет актуальные права перед доступом к сохранённому состоянию. |
| SetPresence | [SetPresenceRequest](#psim-api-v1-SetPresenceRequest) | [SetPresenceResponse](#psim-api-v1-SetPresenceResponse) | SetPresence проверяет актуальные права перед доступом к сохранённому состоянию. |
| GetIncidentSnapshots | [GetIncidentSnapshotsRequest](#psim-api-v1-GetIncidentSnapshotsRequest) | [GetIncidentSnapshotsResponse](#psim-api-v1-GetIncidentSnapshotsResponse) | GetIncidentSnapshots проверяет актуальные права перед доступом к сохранённому состоянию. |
| GetIncidentRecovery | [GetIncidentRecoveryRequest](#psim-api-v1-GetIncidentRecoveryRequest) | [GetIncidentRecoveryResponse](#psim-api-v1-GetIncidentRecoveryResponse) | GetIncidentRecovery проверяет актуальные права перед доступом к сохранённому состоянию. |
| AskAssistant | [AskAssistantRequest](#psim-api-v1-AskAssistantRequest) | [AskAssistantResponse](#psim-api-v1-AskAssistantResponse) | AskAssistant проверяет актуальные права перед доступом к сохранённому состоянию. |
| CreateExercise | [CreateExerciseRequest](#psim-api-v1-CreateExerciseRequest) | [CreateExerciseResponse](#psim-api-v1-CreateExerciseResponse) | CreateExercise проверяет актуальные права перед доступом к сохранённому состоянию. |
| GetExercise | [GetExerciseRequest](#psim-api-v1-GetExerciseRequest) | [GetExerciseResponse](#psim-api-v1-GetExerciseResponse) | GetExercise проверяет актуальные права перед доступом к сохранённому состоянию. |
| ActExercise | [ActExerciseRequest](#psim-api-v1-ActExerciseRequest) | [ActExerciseResponse](#psim-api-v1-ActExerciseResponse) | ActExercise проверяет актуальные права перед доступом к сохранённому состоянию. |





<a name="psim_api_v1_reports-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/api/v1/reports.proto



<a name="psim-api-v1-ExportIncidentReportRequest"></a>

### ExportIncidentReportRequest
ExportIncidentReportRequest - запрос операции ExportIncidentReport.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| filter | [ReportFilter](#psim-api-v1-ReportFilter) |  |  |
| grouping | [ReportGrouping](#psim-api-v1-ReportGrouping) |  |  |






<a name="psim-api-v1-ExportIncidentReportResponse"></a>

### ExportIncidentReportResponse
ExportIncidentReportResponse - ответ операции ExportIncidentReport; тело имеет формат CSV.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| file | [google.api.HttpBody](#google-api-HttpBody) |  |  |






<a name="psim-api-v1-GetIncidentReportRequest"></a>

### GetIncidentReportRequest
GetIncidentReportRequest - запрос операции GetIncidentReport.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| filter | [ReportFilter](#psim-api-v1-ReportFilter) |  |  |
| grouping | [ReportGrouping](#psim-api-v1-ReportGrouping) |  |  |






<a name="psim-api-v1-GetIncidentReportResponse"></a>

### GetIncidentReportResponse
GetIncidentReportResponse - ответ операции GetIncidentReport.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| totals | [IncidentMetrics](#psim-api-v1-IncidentMetrics) |  |  |
| rows | [IncidentMetrics](#psim-api-v1-IncidentMetrics) | repeated |  |
| generated_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-api-v1-IncidentMetrics"></a>

### IncidentMetrics
IncidentMetrics содержит количество инцидентов, время реагирования и соблюдение SLA для группы.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| group | [string](#string) |  | Ключ группы: идентификатор площадки, типа инцидента, приоритет, идентификатор пользователя или дата; пустой для итогов. |
| incidents_created | [uint64](#uint64) |  | Созданы за указанный период. |
| incidents_resolved | [uint64](#uint64) |  | Решены за указанный период, включая созданные ранее инциденты. |
| mtta | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| mttr | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| false_alarm_rate | [double](#double) |  | Доля resolution = false_alarm среди решённых инцидентов, 0..1. |
| ack_sla_compliance | [double](#double) |  | Доля инцидентов, принятых в пределах SLA, 0..1. |
| resolve_sla_compliance | [double](#double) |  | Доля инцидентов, решённых в пределах SLA, 0..1. |
| escalated | [uint64](#uint64) |  | Созданы за указанный период и сейчас имеют ненулевой уровень эскалации. |
| ack_sla_evaluated | [uint64](#uint64) |  | Завершённые или просроченные наблюдения с зафиксированной целью; ожидающие наблюдения исключены. |
| resolve_sla_evaluated | [uint64](#uint64) |  |  |






<a name="psim-api-v1-ReportFilter"></a>

### ReportFilter
ReportFilter ограничивает каждый показатель временем его наблюдения в интервале [from, to).
Показатели создания, принятия и решения используют соответствующие метки времени.
Наблюдения SLA используют более ранний момент завершения или срока; ожидающие исключаются.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| from | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| to | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| site_ids | [string](#string) | repeated |  |
| incident_type_ids | [string](#string) | repeated |  |
| priorities | [psim.common.v1.Priority](#psim-common-v1-Priority) | repeated |  |








<a name="psim-api-v1-ReportGrouping"></a>

### ReportGrouping
ReportGrouping - измерение группировки строк отчёта.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| REPORT_GROUPING_UNSPECIFIED | 0 |  |
| REPORT_GROUPING_NONE | 1 |  |
| REPORT_GROUPING_SITE | 2 |  |
| REPORT_GROUPING_INCIDENT_TYPE | 3 |  |
| REPORT_GROUPING_PRIORITY | 4 |  |
| REPORT_GROUPING_OPERATOR | 5 |  |
| REPORT_GROUPING_DAY | 6 |  |







<a name="psim-api-v1-ReportService"></a>

### ReportService
ReportService формирует отчёты по инцидентам (ADR-028: аналитика в ClickHouse).
Владелец: Projection Service.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| GetIncidentReport | [GetIncidentReportRequest](#psim-api-v1-GetIncidentReportRequest) | [GetIncidentReportResponse](#psim-api-v1-GetIncidentReportResponse) | GetIncidentReport возвращает показатели инцидентов за период. |
| ExportIncidentReport | [ExportIncidentReportRequest](#psim-api-v1-ExportIncidentReportRequest) | [ExportIncidentReportResponse](#psim-api-v1-ExportIncidentReportResponse) | ExportIncidentReport экспортирует тот же отчёт в CSV. |





<a name="psim_processing_v1_mapping-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/processing/v1/mapping.proto



<a name="psim-processing-v1-MappingRule"></a>

### MappingRule
MappingRule сопоставляет сырое событие типу таксономии.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| raw_code | [string](#string) |  | Точный код производителя или префикс, заканчивающийся на &quot;*&quot;. |
| condition | [string](#string) |  | Необязательное условие CEL над переменной &quot;raw&quot;. |
| type | [string](#string) |  | Целевой код типа таксономии. |
| severity | [psim.common.v1.Severity](#psim-common-v1-Severity) |  | Если задано, переопределяет критичность типа по умолчанию. |
| attribute_map | [MappingRule.AttributeMapEntry](#psim-processing-v1-MappingRule-AttributeMapEntry) | repeated | Переименование атрибутов: имя сырого атрибута -&gt; имя нормализованного атрибута. |






<a name="psim-processing-v1-MappingRule-AttributeMapEntry"></a>

### MappingRule.AttributeMapEntry



| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-processing-v1-MappingRuleSet"></a>

### MappingRuleSet
MappingRuleSet - опубликованная версия правил нормализации для одного типа коннектора.
Публикуется в psim.config.v1 с ключом &quot;mapping/&lt;tenant_id&gt;/&lt;connector_type&gt;&quot;.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| mapping_rule_set_id | [string](#string) |  |  |
| version | [uint32](#uint32) |  |  |
| connector_type | [string](#string) |  |  |
| taxonomy_version | [string](#string) |  |  |
| rules | [MappingRule](#psim-processing-v1-MappingRule) | repeated | Упорядочены; применяется первое совпавшее правило. |
| published_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| published_by | [psim.common.v1.Actor](#psim-common-v1-Actor) |  |  |















<a name="psim_processing_v1_rules-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/processing/v1/rules.proto



<a name="psim-processing-v1-AbsenceSpec"></a>

### AbsenceSpec
AbsenceSpec срабатывает, если ожидаемое событие не следует за триггером в пределах окна.
Без триггера срабатывает при отсутствии ожидаемого события в каждом окне (heartbeat).


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| trigger | [EventFilter](#psim-processing-v1-EventFilter) |  |  |
| expected | [EventFilter](#psim-processing-v1-EventFilter) |  |  |






<a name="psim-processing-v1-ConjunctionSpec"></a>

### ConjunctionSpec
ConjunctionSpec срабатывает, когда все части совпали в пределах окна в любом порядке.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| parts | [EventFilter](#psim-processing-v1-EventFilter) | repeated |  |






<a name="psim-processing-v1-CorrelationRule"></a>

### CorrelationRule
CorrelationRule - одна неизменяемая версия правила корреляции (ADR-011).


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| rule_id | [string](#string) |  |  |
| version | [uint32](#uint32) |  |  |
| name | [string](#string) |  |  |
| description | [string](#string) |  |  |
| state | [RuleState](#psim-processing-v1-RuleState) |  |  |
| filter | [EventFilter](#psim-processing-v1-EventFilter) |  | События, которые вообще рассматриваются правилом. |
| correlation_scope | [CorrelationScope](#psim-processing-v1-CorrelationScope) |  |  |
| window | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| allowed_lateness | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| suppression | [google.protobuf.Duration](#google-protobuf-Duration) |  | Подавлять повторные сигналы для того же ключа корреляции в течение этого интервала. |
| single | [SingleSpec](#psim-processing-v1-SingleSpec) |  |  |
| conjunction | [ConjunctionSpec](#psim-processing-v1-ConjunctionSpec) |  |  |
| sequence | [SequenceSpec](#psim-processing-v1-SequenceSpec) |  |  |
| threshold | [ThresholdSpec](#psim-processing-v1-ThresholdSpec) |  |  |
| absence | [AbsenceSpec](#psim-processing-v1-AbsenceSpec) |  |  |
| output | [RuleOutput](#psim-processing-v1-RuleOutput) |  |  |
| created_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| created_by | [psim.common.v1.Actor](#psim-common-v1-Actor) |  |  |






<a name="psim-processing-v1-EventFilter"></a>

### EventFilter
EventFilter выбирает события по типу таксономии, критичности, локации и условию CEL.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| types | [string](#string) | repeated | Коды таксономии или префиксы, заканчивающиеся на &quot;.*&quot;, например &quot;intrusion.*&quot;. |
| min_severity | [psim.common.v1.Severity](#psim-common-v1-Severity) |  |  |
| site_ids | [string](#string) | repeated |  |
| zone_ids | [string](#string) | repeated |  |
| condition | [string](#string) |  | Необязательное выражение CEL над переменной &quot;event&quot;. |






<a name="psim-processing-v1-RuleOutput"></a>

### RuleOutput
RuleOutput описывает сигнал, формируемый правилом.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_type_id | [string](#string) |  |  |
| priority | [psim.common.v1.Priority](#psim-common-v1-Priority) |  | Фиксированный приоритет, используемый при пустом priority_expression. |
| priority_expression | [string](#string) |  | Необязательное выражение CEL, возвращающее 1..4. |
| grouping_scope | [CorrelationScope](#psim-processing-v1-CorrelationScope) |  | Область ключа группировки; по умолчанию равна области корреляции. |
| summary_template | [string](#string) |  | Шаблон сводки; может ссылаться на поля события и правила. |






<a name="psim-processing-v1-RuleSet"></a>

### RuleSet
RuleSet - набор версий правил, активных для тенанта.
Публикуется в psim.config.v1 с ключом &quot;ruleset/&lt;tenant_id&gt;&quot;.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| rule_set_id | [string](#string) |  |  |
| version | [uint64](#uint64) |  |  |
| rules | [CorrelationRule](#psim-processing-v1-CorrelationRule) | repeated |  |
| activated_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| activated_by | [psim.common.v1.Actor](#psim-common-v1-Actor) |  |  |






<a name="psim-processing-v1-SequenceSpec"></a>

### SequenceSpec
SequenceSpec срабатывает при совпадении шагов в указанном порядке в пределах окна.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| steps | [EventFilter](#psim-processing-v1-EventFilter) | repeated |  |






<a name="psim-processing-v1-SingleSpec"></a>

### SingleSpec
SingleSpec срабатывает для каждого подходящего события.






<a name="psim-processing-v1-ThresholdSpec"></a>

### ThresholdSpec
ThresholdSpec срабатывает, если в пределах окна произошло не менее count подходящих событий.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| count | [uint32](#uint32) |  |  |
| distinct_by | [string](#string) |  | Считать различные значения этого поля, &quot;device_id&quot;, &quot;source_id&quot; или атрибута, вместо событий. |








<a name="psim-processing-v1-CorrelationScope"></a>

### CorrelationScope
CorrelationScope определяет ключ корреляции правила.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| CORRELATION_SCOPE_UNSPECIFIED | 0 |  |
| CORRELATION_SCOPE_SOURCE | 1 |  |
| CORRELATION_SCOPE_DEVICE | 2 |  |
| CORRELATION_SCOPE_ZONE | 3 |  |
| CORRELATION_SCOPE_FLOOR | 4 |  |
| CORRELATION_SCOPE_BUILDING | 5 |  |
| CORRELATION_SCOPE_SITE | 6 |  |



<a name="psim-processing-v1-RuleState"></a>

### RuleState
RuleState - состояние жизненного цикла версии правила.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| RULE_STATE_UNSPECIFIED | 0 |  |
| RULE_STATE_DRAFT | 1 |  |
| RULE_STATE_TESTING | 2 |  |
| RULE_STATE_ACTIVE | 3 |  |
| RULE_STATE_ARCHIVED | 4 |  |










<a name="psim_api_v1_rules-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/api/v1/rules.proto



<a name="psim-api-v1-ActivateRuleSetRequest"></a>

### ActivateRuleSetRequest
ActivateRuleSetRequest - запрос операции ActivateRuleSet.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| rules | [RuleVersionRef](#psim-api-v1-RuleVersionRef) | repeated | Версии правил для активации; каждая должна находиться в testing или active. |
| expected_rule_set_version | [uint64](#uint64) |  | Версия текущего активного набора для оптимистического контроля конкурентных изменений. |






<a name="psim-api-v1-ActivateRuleSetResponse"></a>

### ActivateRuleSetResponse
ActivateRuleSetResponse - ответ операции ActivateRuleSet.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| rule_set | [psim.processing.v1.RuleSet](#psim-processing-v1-RuleSet) |  |  |






<a name="psim-api-v1-ArchiveRuleRequest"></a>

### ArchiveRuleRequest
ArchiveRuleRequest - запрос операции ArchiveRule.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| rule_id | [string](#string) |  |  |






<a name="psim-api-v1-ArchiveRuleResponse"></a>

### ArchiveRuleResponse
ArchiveRuleResponse - ответ операции ArchiveRule.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| rule | [psim.processing.v1.CorrelationRule](#psim-processing-v1-CorrelationRule) |  |  |






<a name="psim-api-v1-DryRun"></a>

### DryRun
DryRun - пробный запуск правила на истории.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| dry_run_id | [string](#string) |  |  |
| rule_id | [string](#string) |  |  |
| version | [uint32](#uint32) |  |  |
| state | [DryRunState](#psim-api-v1-DryRunState) |  |  |
| events_processed | [uint64](#uint64) |  |  |
| signals_produced | [uint64](#uint64) |  |  |
| started_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| finished_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| failure | [string](#string) |  | Безопасное диагностическое сообщение для неуспешных запусков; без учётных данных и входного содержимого. |
| source | [DryRunSource](#psim-api-v1-DryRunSource) |  |  |






<a name="psim-api-v1-DryRunDecision"></a>

### DryRunDecision
DryRunDecision сохраняет подавленные результаты и причину подавления.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| signal_id | [string](#string) |  |  |
| signal | [psim.processing.v1.Signal](#psim-processing-v1-Signal) |  |  |
| suppressed | [bool](#bool) |  |  |
| maintenance_ids | [string](#string) | repeated |  |
| maintenance_versions | [MaintenanceVersionRef](#psim-api-v1-MaintenanceVersionRef) | repeated |  |






<a name="psim-api-v1-GetActiveRuleSetRequest"></a>

### GetActiveRuleSetRequest
GetActiveRuleSetRequest - запрос операции GetActiveRuleSet.






<a name="psim-api-v1-GetActiveRuleSetResponse"></a>

### GetActiveRuleSetResponse
GetActiveRuleSetResponse - ответ операции GetActiveRuleSet.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| rule_set | [psim.processing.v1.RuleSet](#psim-processing-v1-RuleSet) |  |  |






<a name="psim-api-v1-GetDryRunRequest"></a>

### GetDryRunRequest
GetDryRunRequest - запрос операции GetDryRun.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| dry_run_id | [string](#string) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-GetDryRunResponse"></a>

### GetDryRunResponse
GetDryRunResponse - ответ операции GetDryRun.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| dry_run | [DryRun](#psim-api-v1-DryRun) |  |  |
| signals | [psim.processing.v1.Signal](#psim-processing-v1-Signal) | repeated |  |
| next_page_token | [string](#string) |  |  |
| decisions | [DryRunDecision](#psim-api-v1-DryRunDecision) | repeated |  |






<a name="psim-api-v1-GetMappingRuleSetRequest"></a>

### GetMappingRuleSetRequest
GetMappingRuleSetRequest - запрос операции GetMappingRuleSet.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| connector_type | [string](#string) |  |  |






<a name="psim-api-v1-GetMappingRuleSetResponse"></a>

### GetMappingRuleSetResponse
GetMappingRuleSetResponse - ответ операции GetMappingRuleSet.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| mapping_rule_set | [psim.processing.v1.MappingRuleSet](#psim-processing-v1-MappingRuleSet) |  |  |






<a name="psim-api-v1-GetRuleRequest"></a>

### GetRuleRequest
GetRuleRequest - запрос операции GetRule.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| rule_id | [string](#string) |  |  |
| version | [uint32](#uint32) |  |  |






<a name="psim-api-v1-GetRuleResponse"></a>

### GetRuleResponse
GetRuleResponse - ответ операции GetRule.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| rule | [psim.processing.v1.CorrelationRule](#psim-processing-v1-CorrelationRule) |  |  |






<a name="psim-api-v1-ListMappingRuleSetsRequest"></a>

### ListMappingRuleSetsRequest
ListMappingRuleSetsRequest - запрос операции ListMappingRuleSets.






<a name="psim-api-v1-ListMappingRuleSetsResponse"></a>

### ListMappingRuleSetsResponse
ListMappingRuleSetsResponse - ответ операции ListMappingRuleSets.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| mapping_rule_sets | [psim.processing.v1.MappingRuleSet](#psim-processing-v1-MappingRuleSet) | repeated |  |






<a name="psim-api-v1-ListRulesRequest"></a>

### ListRulesRequest
ListRulesRequest - запрос операции ListRules.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| states | [psim.processing.v1.RuleState](#psim-processing-v1-RuleState) | repeated |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListRulesResponse"></a>

### ListRulesResponse
ListRulesResponse - ответ операции ListRules.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| rules | [psim.processing.v1.CorrelationRule](#psim-processing-v1-CorrelationRule) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-MaintenanceVersionRef"></a>

### MaintenanceVersionRef
MaintenanceVersionRef определяет точную версию политики, использованную для решения.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| maintenance_id | [string](#string) |  |  |
| version | [uint64](#uint64) |  |  |






<a name="psim-api-v1-PublishMappingRuleSetRequest"></a>

### PublishMappingRuleSetRequest
PublishMappingRuleSetRequest - запрос операции PublishMappingRuleSet.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| connector_type | [string](#string) |  |  |
| mapping_rule_set | [psim.processing.v1.MappingRuleSet](#psim-processing-v1-MappingRuleSet) |  |  |






<a name="psim-api-v1-PublishMappingRuleSetResponse"></a>

### PublishMappingRuleSetResponse
PublishMappingRuleSetResponse - ответ операции PublishMappingRuleSet.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| mapping_rule_set | [psim.processing.v1.MappingRuleSet](#psim-processing-v1-MappingRuleSet) |  |  |






<a name="psim-api-v1-RuleVersionRef"></a>

### RuleVersionRef
RuleVersionRef ссылается на версию правила.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| rule_id | [string](#string) |  |  |
| version | [uint32](#uint32) |  |  |






<a name="psim-api-v1-SaveRuleDraftRequest"></a>

### SaveRuleDraftRequest
SaveRuleDraftRequest - запрос операции SaveRuleDraft.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| rule | [psim.processing.v1.CorrelationRule](#psim-processing-v1-CorrelationRule) |  |  |






<a name="psim-api-v1-SaveRuleDraftResponse"></a>

### SaveRuleDraftResponse
SaveRuleDraftResponse - ответ операции SaveRuleDraft.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| rule | [psim.processing.v1.CorrelationRule](#psim-processing-v1-CorrelationRule) |  |  |






<a name="psim-api-v1-StartDryRunRequest"></a>

### StartDryRunRequest
StartDryRunRequest - запрос операции StartDryRun.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| rule_id | [string](#string) |  |  |
| version | [uint32](#uint32) |  |  |
| from | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  | Диапазон времени событий, ограниченный retention нормализованных событий. |
| to | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| source | [DryRunSource](#psim-api-v1-DryRunSource) |  |  |






<a name="psim-api-v1-StartDryRunResponse"></a>

### StartDryRunResponse
StartDryRunResponse - ответ операции StartDryRun.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| dry_run | [DryRun](#psim-api-v1-DryRun) |  |  |






<a name="psim-api-v1-ValidateRuleRequest"></a>

### ValidateRuleRequest
ValidateRuleRequest - запрос операции ValidateRule.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| rule_id | [string](#string) |  |  |
| version | [uint32](#uint32) |  |  |






<a name="psim-api-v1-ValidateRuleResponse"></a>

### ValidateRuleResponse
ValidateRuleResponse - ответ операции ValidateRule.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| valid | [bool](#bool) |  |  |
| violations | [FieldViolation](#psim-api-v1-FieldViolation) | repeated |  |
| rule | [psim.processing.v1.CorrelationRule](#psim-processing-v1-CorrelationRule) |  |  |








<a name="psim-api-v1-DryRunSource"></a>

### DryRunSource
DryRunSource выбирает исторический источник только для чтения. Неуказанное значение выбирает Kafka.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| DRY_RUN_SOURCE_UNSPECIFIED | 0 |  |
| DRY_RUN_SOURCE_KAFKA | 1 |  |
| DRY_RUN_SOURCE_CLICKHOUSE | 2 |  |



<a name="psim-api-v1-DryRunState"></a>

### DryRunState
DryRunState - состояние пробного запуска.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| DRY_RUN_STATE_UNSPECIFIED | 0 |  |
| DRY_RUN_STATE_RUNNING | 1 |  |
| DRY_RUN_STATE_COMPLETED | 2 |  |
| DRY_RUN_STATE_FAILED | 3 |  |







<a name="psim-api-v1-CorrelationRuleService"></a>

### CorrelationRuleService
CorrelationRuleService управляет правилами корреляции и активным набором правил.
Владелец: Correlation Engine, модуль управления правилами.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| ListRules | [ListRulesRequest](#psim-api-v1-ListRulesRequest) | [ListRulesResponse](#psim-api-v1-ListRulesResponse) | ListRules возвращает последнюю версию каждого правила. |
| GetRule | [GetRuleRequest](#psim-api-v1-GetRuleRequest) | [GetRuleResponse](#psim-api-v1-GetRuleResponse) | GetRule возвращает версию правила; при версии 0 выбирается последняя. |
| SaveRuleDraft | [SaveRuleDraftRequest](#psim-api-v1-SaveRuleDraftRequest) | [SaveRuleDraftResponse](#psim-api-v1-SaveRuleDraftResponse) | SaveRuleDraft создаёт правило или его новую черновую версию. |
| ValidateRule | [ValidateRuleRequest](#psim-api-v1-ValidateRuleRequest) | [ValidateRuleResponse](#psim-api-v1-ValidateRuleResponse) | ValidateRule проверяет черновик и при успехе переводит его в testing (R1). |
| StartDryRun | [StartDryRunRequest](#psim-api-v1-StartDryRunRequest) | [StartDryRunResponse](#psim-api-v1-StartDryRunResponse) | StartDryRun выполняет версию правила на исторических событиях без публикации сигналов. |
| GetDryRun | [GetDryRunRequest](#psim-api-v1-GetDryRunRequest) | [GetDryRunResponse](#psim-api-v1-GetDryRunResponse) | GetDryRun возвращает ход пробного запуска и сформированные сигналы. |
| ArchiveRule | [ArchiveRuleRequest](#psim-api-v1-ArchiveRuleRequest) | [ArchiveRuleResponse](#psim-api-v1-ArchiveRuleResponse) | ArchiveRule архивирует правило; оно исключается из следующего набора правил. |
| GetActiveRuleSet | [GetActiveRuleSetRequest](#psim-api-v1-GetActiveRuleSetRequest) | [GetActiveRuleSetResponse](#psim-api-v1-GetActiveRuleSetResponse) | GetActiveRuleSet возвращает активный набор правил. |
| ActivateRuleSet | [ActivateRuleSetRequest](#psim-api-v1-ActivateRuleSetRequest) | [ActivateRuleSetResponse](#psim-api-v1-ActivateRuleSetResponse) | ActivateRuleSet атомарно активирует новый набор версий правил (RS1-RS3). |


<a name="psim-api-v1-MappingService"></a>

### MappingService
MappingService управляет правилами нормализации типов коннекторов. Владелец: Resource Catalog; публикует в psim.config.v1.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| ListMappingRuleSets | [ListMappingRuleSetsRequest](#psim-api-v1-ListMappingRuleSetsRequest) | [ListMappingRuleSetsResponse](#psim-api-v1-ListMappingRuleSetsResponse) | ListMappingRuleSets возвращает активный набор правил нормализации для каждого типа коннектора. |
| GetMappingRuleSet | [GetMappingRuleSetRequest](#psim-api-v1-GetMappingRuleSetRequest) | [GetMappingRuleSetResponse](#psim-api-v1-GetMappingRuleSetResponse) | GetMappingRuleSet возвращает активный набор правил нормализации для типа коннектора. |
| PublishMappingRuleSet | [PublishMappingRuleSetRequest](#psim-api-v1-PublishMappingRuleSetRequest) | [PublishMappingRuleSetResponse](#psim-api-v1-PublishMappingRuleSetResponse) | PublishMappingRuleSet проверяет и публикует новую версию (M1-M3). |





<a name="psim_config_v1_config-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/config/v1/config.proto



<a name="psim-config-v1-ConfigRecord"></a>

### ConfigRecord
ConfigRecord - значение компактируемого однопартиционного топика psim.config.v1 с ключом config_key.
Каждая запись содержит полный активный объект; потребители применяют его атомарно между входными батчами.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| rule_set | [psim.processing.v1.RuleSet](#psim-processing-v1-RuleSet) |  | Ключ &quot;ruleset/&lt;tenant_id&gt;&quot;. |
| mapping_rule_set | [psim.processing.v1.MappingRuleSet](#psim-processing-v1-MappingRuleSet) |  | Ключ &quot;mapping/&lt;tenant_id&gt;/&lt;connector_type&gt;&quot;. |
| retention | [RetentionSettings](#psim-config-v1-RetentionSettings) |  | Ключ &quot;retention/&lt;tenant_id&gt;&quot;. |
| processing | [ProcessingSettings](#psim-config-v1-ProcessingSettings) |  | Ключ &quot;processing/&lt;tenant_id&gt;&quot;. |
| maintenance_window | [psim.catalog.v1.MaintenanceWindow](#psim-catalog-v1-MaintenanceWindow) |  | Ключ &quot;maintenance/&lt;tenant_id&gt;/&lt;maintenance_id&gt;&quot;; конечные версии сохраняются для replay. |
| incident_type | [psim.incident.v1.IncidentType](#psim-incident-v1-IncidentType) |  | Ключ &quot;incident-type/&lt;tenant_id&gt;/&lt;incident_type_id&gt;&quot;. |






<a name="psim-config-v1-ProcessingSettings"></a>

### ProcessingSettings
ProcessingSettings - параметры обработки для всего тенанта.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| significance_threshold | [psim.common.v1.Severity](#psim-common-v1-Severity) |  | События с этой или более высокой критичностью являются значимыми (E3). |
| max_clock_skew | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| max_buffer_replay_age | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| source_idle_timeout | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| version | [uint64](#uint64) |  |  |






<a name="psim-config-v1-RetentionSettings"></a>

### RetentionSettings
RetentionSettings - сроки хранения данных тенанта.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| event_history_all | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| event_history_significant | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| incidents | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| audit | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| version | [uint64](#uint64) |  |  |















<a name="psim_api_v1_settings-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/api/v1/settings.proto



<a name="psim-api-v1-GetSettingsRequest"></a>

### GetSettingsRequest
GetSettingsRequest - запрос операции GetSettings.






<a name="psim-api-v1-GetSettingsResponse"></a>

### GetSettingsResponse
GetSettingsResponse - ответ операции GetSettings.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| retention | [psim.config.v1.RetentionSettings](#psim-config-v1-RetentionSettings) |  |  |
| processing | [psim.config.v1.ProcessingSettings](#psim-config-v1-ProcessingSettings) |  |  |






<a name="psim-api-v1-UpdateProcessingSettingsRequest"></a>

### UpdateProcessingSettingsRequest
UpdateProcessingSettingsRequest - запрос операции UpdateProcessingSettings.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| processing | [psim.config.v1.ProcessingSettings](#psim-config-v1-ProcessingSettings) |  |  |






<a name="psim-api-v1-UpdateProcessingSettingsResponse"></a>

### UpdateProcessingSettingsResponse
UpdateProcessingSettingsResponse - ответ операции UpdateProcessingSettings.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| processing | [psim.config.v1.ProcessingSettings](#psim-config-v1-ProcessingSettings) |  |  |






<a name="psim-api-v1-UpdateRetentionSettingsRequest"></a>

### UpdateRetentionSettingsRequest
UpdateRetentionSettingsRequest - запрос операции UpdateRetentionSettings.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| retention | [psim.config.v1.RetentionSettings](#psim-config-v1-RetentionSettings) |  |  |






<a name="psim-api-v1-UpdateRetentionSettingsResponse"></a>

### UpdateRetentionSettingsResponse
UpdateRetentionSettingsResponse - ответ операции UpdateRetentionSettings.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| retention | [psim.config.v1.RetentionSettings](#psim-config-v1-RetentionSettings) |  |  |












<a name="psim-api-v1-SettingsService"></a>

### SettingsService
SettingsService управляет сроками хранения и параметрами обработки тенанта.
Владелец: Resource Catalog; изменения публикуются в psim.config.v1.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| GetSettings | [GetSettingsRequest](#psim-api-v1-GetSettingsRequest) | [GetSettingsResponse](#psim-api-v1-GetSettingsResponse) | GetSettings возвращает текущие настройки. |
| UpdateRetentionSettings | [UpdateRetentionSettingsRequest](#psim-api-v1-UpdateRetentionSettingsRequest) | [UpdateRetentionSettingsResponse](#psim-api-v1-UpdateRetentionSettingsResponse) | UpdateRetentionSettings заменяет настройки retention; требуется If-Match. |
| UpdateProcessingSettings | [UpdateProcessingSettingsRequest](#psim-api-v1-UpdateProcessingSettingsRequest) | [UpdateProcessingSettingsResponse](#psim-api-v1-UpdateProcessingSettingsResponse) | UpdateProcessingSettings заменяет настройки обработки; требуется If-Match. |





<a name="psim_connector_v1_administration-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/connector/v1/administration.proto



<a name="psim-connector-v1-ManageRequest"></a>

### ManageRequest
ManageRequest идемпотентен в пределах тенанта, оператора и request_id.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| request_id | [string](#string) |  |  |
| action | [RegistrationAction](#psim-connector-v1-RegistrationAction) |  |  |
| connector | [psim.catalog.v1.Connector](#psim-catalog-v1-Connector) |  | Только регистрация использует name, connector_type и limits; учётные данные выводятся из PEM. |
| expected_version | [uint64](#uint64) |  |  |
| certificate_pem | [string](#string) |  | Только открытый сертификат; закрытый ключ никогда не покидает коннектор. |
| fingerprint | [string](#string) |  | Обязательно для отзыва, игнорируется для других действий. |
| previous_not_after | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  | Необязательное сокращение срока действия прежних сертификатов при ротации. |
| reason | [string](#string) |  | Объяснение оператора; при наличии включается в аудит. |
| expected_fingerprint | [string](#string) |  | Необязательная проверка соответствия открытого сертификата ожидаемому отпечатку. |






<a name="psim-connector-v1-ManageResponse"></a>

### ManageResponse
ManageResponse возвращает зафиксированную регистрацию и глобальную ревизию применения.
Ошибка срока ожидания может означать, что изменение зафиксировано, но применение ещё не завершено;
для выяснения исхода повторите точный запрос с прежним request_id.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| connector | [psim.catalog.v1.Connector](#psim-catalog-v1-Connector) |  |  |
| revision | [uint64](#uint64) |  |  |








<a name="psim-connector-v1-RegistrationAction"></a>

### RegistrationAction
RegistrationAction выбирает изменение учётных данных или регистрации.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| REGISTRATION_ACTION_UNSPECIFIED | 0 |  |
| REGISTRATION_ACTION_REGISTER | 1 |  |
| REGISTRATION_ACTION_ROTATE | 2 |  |
| REGISTRATION_ACTION_REVOKE | 3 |  |
| REGISTRATION_ACTION_RECOVER | 4 |  |
| REGISTRATION_ACTION_DISABLE | 5 |  |
| REGISTRATION_ACTION_INSPECT | 6 |  |







<a name="psim-connector-v1-ConnectorAdministrationService"></a>

### ConnectorAdministrationService
ConnectorAdministrationService управляет регистрациями через mTLS и делегированные JWT оператора.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| Manage | [ManageRequest](#psim-connector-v1-ManageRequest) | [ManageResponse](#psim-connector-v1-ManageResponse) | Manage фиксирует версионированное изменение и ждёт его применения активными шлюзами. |





<a name="psim_connector_v1_connector-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/connector/v1/connector.proto



<a name="psim-connector-v1-BatchAck"></a>

### BatchAck
BatchAck сообщает, что батч долговечно записан с acks=all или отклонён.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| batch_seq | [uint64](#uint64) |  |  |
| accepted | [uint32](#uint32) |  |  |
| rejections | [EventRejection](#psim-connector-v1-EventRejection) | repeated | Окончательно отклонённые события; повторная отправка запрещена. |






<a name="psim-connector-v1-Command"></a>

### Command
Command запрашивает действие коннектора на устройстве. Исполнять не более одного раза для command_id.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |
| device_external_id | [string](#string) |  |  |
| capability | [string](#string) |  |  |
| parameters | [Command.ParametersEntry](#psim-connector-v1-Command-ParametersEntry) | repeated |  |
| deadline_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-connector-v1-Command-ParametersEntry"></a>

### Command.ParametersEntry



| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-connector-v1-CommandAck"></a>

### CommandAck
CommandAck подтверждает получение команды коннектором.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |






<a name="psim-connector-v1-CommandResult"></a>

### CommandResult
CommandResult сообщает исход команды.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |
| success | [bool](#bool) |  |  |
| code | [string](#string) |  | Код ошибки при success = false. |
| message | [string](#string) |  |  |






<a name="psim-connector-v1-ConfigUpdate"></a>

### ConfigUpdate
ConfigUpdate отправляет конфигурацию каталога коннектора.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| config_version | [uint64](#uint64) |  |  |
| devices | [ConnectorDevice](#psim-connector-v1-ConnectorDevice) | repeated |  |






<a name="psim-connector-v1-ConnectorDevice"></a>

### ConnectorDevice
ConnectorDevice - устройство, обслуживаемое коннектором.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| device_id | [string](#string) |  |  |
| external_id | [string](#string) |  |  |
| name | [string](#string) |  |  |
| device_type_code | [string](#string) |  |  |
| source_refs | [string](#string) | repeated |  |
| capabilities | [string](#string) | repeated |  |
| enabled | [bool](#bool) |  |  |






<a name="psim-connector-v1-ConnectorEvent"></a>

### ConnectorEvent
ConnectorEvent - одно событие источника.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| event_id | [string](#string) |  | UUIDv7, назначенный при создании события; не меняется при повторной отправке. |
| source_ref | [string](#string) |  | Ссылка источника, известная коннектору, Source.external_ref. |
| source_epoch | [uint32](#uint32) |  |  |
| source_seq | [uint64](#uint64) |  | Строго возрастает в пределах source_ref и source_epoch. |
| occurred_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| raw_code | [string](#string) |  |  |
| attributes | [ConnectorEvent.AttributesEntry](#psim-connector-v1-ConnectorEvent-AttributesEntry) | repeated |  |
| traceparent | [string](#string) |  | Необязательный W3C traceparent span коннектора. |
| live_observation | [bool](#bool) |  | False для событий, записанных без активной сессии или повторно отправленных после переподключения. Они пополняют историю, но не устанавливают текущее состояние. |
| observation_session_id | [string](#string) |  | Сессия шлюза, в которой получено наблюдение; прежние сессии не могут обновлять актуальность состояния. |






<a name="psim-connector-v1-ConnectorEvent-AttributesEntry"></a>

### ConnectorEvent.AttributesEntry



| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-connector-v1-Credits"></a>

### Credits
Credits предоставляет дополнительное количество событий для отправки.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| grant | [uint32](#uint32) |  |  |






<a name="psim-connector-v1-EventBatch"></a>

### EventBatch
EventBatch - батч событий; коннектор не должен превышать выданные credits.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| batch_seq | [uint64](#uint64) |  | Монотонно возрастает в пределах сессии; повторяется в BatchAck. |
| events | [ConnectorEvent](#psim-connector-v1-ConnectorEvent) | repeated |  |






<a name="psim-connector-v1-EventRejection"></a>

### EventRejection
EventRejection - окончательное отклонение одного события.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| event_id | [string](#string) |  |  |
| code | [string](#string) |  | Код ошибки из каталога, например &quot;INGEST_INVALID_EVENT&quot;. |
| message | [string](#string) |  |  |






<a name="psim-connector-v1-Goodbye"></a>

### Goodbye
Goodbye корректно закрывает сессию.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| reason | [GoodbyeReason](#psim-connector-v1-GoodbyeReason) |  |  |
| message | [string](#string) |  |  |






<a name="psim-connector-v1-Heartbeat"></a>

### Heartbeat
Heartbeat поддерживает активность сессии; отправляется обеими сторонами.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| sent_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-connector-v1-Register"></a>

### Register
Register открывает сессию.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| protocol_versions | [string](#string) | repeated | Версии протокола, поддерживаемые коннектором, например [&quot;1.0&quot;]. |
| connector_version | [string](#string) |  |  |
| instance_id | [string](#string) |  | Уникален для каждого запущенного процесса коннектора, для диагностики. |
| config_version | [uint64](#uint64) |  | Версия конфигурации каталога, уже имеющаяся у коннектора. |
| capabilities | [string](#string) | repeated | Необязательные возможности: наблюдения &quot;state.v1&quot;, запросы &quot;snapshot.v1&quot; и исполнение &quot;commands.v1&quot;. |






<a name="psim-connector-v1-Registered"></a>

### Registered
Registered подтверждает сессию.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| session_id | [string](#string) |  |  |
| connector_id | [string](#string) |  |  |
| protocol_version | [string](#string) |  |  |
| initial_credits | [uint32](#uint32) |  | Количество событий, которое коннектор может отправить до следующего Credits. |
| heartbeat_interval | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| session_timeout | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| max_batch_events | [uint32](#uint32) |  |  |
| max_batch_bytes | [uint32](#uint32) |  |  |
| capabilities | [string](#string) | repeated | Пересечение поддерживаемых возможностей; отсутствует у шлюза протокола 1.0. |






<a name="psim-connector-v1-SessionRequest"></a>

### SessionRequest
SessionRequest - сообщение от коннектора шлюзу.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| register | [Register](#psim-connector-v1-Register) |  |  |
| heartbeat | [Heartbeat](#psim-connector-v1-Heartbeat) |  |  |
| event_batch | [EventBatch](#psim-connector-v1-EventBatch) |  |  |
| command_ack | [CommandAck](#psim-connector-v1-CommandAck) |  |  |
| command_result | [CommandResult](#psim-connector-v1-CommandResult) |  |  |
| goodbye | [Goodbye](#psim-connector-v1-Goodbye) |  |  |
| state_report | [StateReport](#psim-connector-v1-StateReport) |  |  |






<a name="psim-connector-v1-SessionResponse"></a>

### SessionResponse
SessionResponse - сообщение от шлюза коннектору.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| registered | [Registered](#psim-connector-v1-Registered) |  |  |
| heartbeat | [Heartbeat](#psim-connector-v1-Heartbeat) |  |  |
| batch_ack | [BatchAck](#psim-connector-v1-BatchAck) |  |  |
| credits | [Credits](#psim-connector-v1-Credits) |  |  |
| command | [Command](#psim-connector-v1-Command) |  |  |
| config_update | [ConfigUpdate](#psim-connector-v1-ConfigUpdate) |  |  |
| goodbye | [Goodbye](#psim-connector-v1-Goodbye) |  |  |






<a name="psim-connector-v1-SourceCursor"></a>

### SourceCursor
SourceCursor определяет границу порядка, зафиксированную до опроса оборудования.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| source_ref | [string](#string) |  |  |
| source_epoch | [uint32](#uint32) |  |  |
| source_seq | [uint64](#uint64) |  |  |






<a name="psim-connector-v1-StateObservation"></a>

### StateObservation
StateObservation - текущее наблюдение оборудования, никогда не replay исторических событий.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| cursor | [SourceCursor](#psim-connector-v1-SourceCursor) |  |  |
| observed_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| availability | [DeviceAvailability](#psim-connector-v1-DeviceAvailability) |  |  |
| values | [StateObservation.ValuesEntry](#psim-connector-v1-StateObservation-ValuesEntry) | repeated |  |






<a name="psim-connector-v1-StateObservation-ValuesEntry"></a>

### StateObservation.ValuesEntry



| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-connector-v1-StateReport"></a>

### StateReport
StateReport отправляется только после согласования state.v1. В каждой сессии запрашивается свежий snapshot.
Отчёты snapshot дополнительно требуют snapshot.v1; источники без опроса остаются unknown.
Курсоры snapshot фиксируются до опроса: более поздние события имеют приоритет над старым snapshot.
Отсутствующие источники остаются unknown; complete не означает, что все устройства online.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| snapshot_id | [string](#string) |  |  |
| snapshot | [bool](#bool) |  |  |
| complete | [bool](#bool) |  |  |
| observations | [StateObservation](#psim-connector-v1-StateObservation) | repeated |  |








<a name="psim-connector-v1-DeviceAvailability"></a>

### DeviceAvailability
DeviceAvailability описывает связность оборудования независимо от связности коннектора.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| DEVICE_AVAILABILITY_UNSPECIFIED | 0 |  |
| DEVICE_AVAILABILITY_UNKNOWN | 1 |  |
| DEVICE_AVAILABILITY_ONLINE | 2 |  |
| DEVICE_AVAILABILITY_OFFLINE | 3 |  |



<a name="psim-connector-v1-GoodbyeReason"></a>

### GoodbyeReason
GoodbyeReason - причина закрытия сессии.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| GOODBYE_REASON_UNSPECIFIED | 0 |  |
| GOODBYE_REASON_SHUTDOWN | 1 |  |
| GOODBYE_REASON_DRAINING | 2 |  |
| GOODBYE_REASON_SUPERSEDED | 3 |  |
| GOODBYE_REASON_REVOKED | 4 |  |
| GOODBYE_REASON_PROTOCOL_ERROR | 5 |  |







<a name="psim-connector-v1-ConnectorGatewayService"></a>

### ConnectorGatewayService
ConnectorGatewayService - протокол коннектора v1 (ADR-017).
Транспорт: gRPC поверх TLS 1.3 с взаимной аутентификацией TLS; клиентский сертификат определяет коннектор.

| Метод | Тип запроса | Тип ответа | Описание |
| ----------- | ------------ | ------------- | ------------|
| Session | [SessionRequest](#psim-connector-v1-SessionRequest) stream | [SessionResponse](#psim-connector-v1-SessionResponse) stream | Session - единственный двунаправленный поток сессии коннектора. Первое сообщение клиента должно быть Register; сервер отвечает Registered или закрывает поток. |





<a name="psim_incident_v1_events-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/incident/v1/events.proto



<a name="psim-incident-v1-IncidentAcknowledged"></a>

### IncidentAcknowledged
IncidentAcknowledged публикуется при принятии инцидента оператором.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-incident-v1-IncidentAssigned"></a>

### IncidentAssigned
IncidentAssigned публикуется при назначении или переназначении.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| previous_assignee_id | [string](#string) |  |  |






<a name="psim-incident-v1-IncidentBlockingStepsChanged"></a>

### IncidentBlockingStepsChanged
IncidentBlockingStepsChanged публикуется при изменении blocking_steps по событиям реагирования.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-incident-v1-IncidentClosed"></a>

### IncidentClosed
IncidentClosed публикуется при переходе в closed.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-incident-v1-IncidentCommandUpdated"></a>

### IncidentCommandUpdated
IncidentCommandUpdated добавляет изменение команды устройства в хронологию инцидента.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| entry | [TimelineEntry](#psim-incident-v1-TimelineEntry) |  |  |






<a name="psim-incident-v1-IncidentCommentAdded"></a>

### IncidentCommentAdded
IncidentCommentAdded публикуется при добавлении комментария.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| entry | [TimelineEntry](#psim-incident-v1-TimelineEntry) |  |  |






<a name="psim-incident-v1-IncidentCreated"></a>

### IncidentCreated
IncidentCreated публикуется, когда сигнал открывает новый инцидент.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| signal_id | [string](#string) |  |  |






<a name="psim-incident-v1-IncidentEscalated"></a>

### IncidentEscalated
IncidentEscalated публикуется при применении уровня эскалации.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| sla_kind | [string](#string) |  | &quot;ack&quot; или &quot;resolve&quot;. |






<a name="psim-incident-v1-IncidentEventRecord"></a>

### IncidentEventRecord
IncidentEventRecord - значение топика psim.incidents.events.v1 с ключом incident_id.
Каждое событие содержит состояние инцидента после изменения; envelope.sequence = incident.version.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| incident_created | [IncidentCreated](#psim-incident-v1-IncidentCreated) |  |  |
| signal_attached | [SignalAttached](#psim-incident-v1-SignalAttached) |  |  |
| incident_priority_changed | [IncidentPriorityChanged](#psim-incident-v1-IncidentPriorityChanged) |  |  |
| incident_acknowledged | [IncidentAcknowledged](#psim-incident-v1-IncidentAcknowledged) |  |  |
| incident_work_started | [IncidentWorkStarted](#psim-incident-v1-IncidentWorkStarted) |  |  |
| incident_assigned | [IncidentAssigned](#psim-incident-v1-IncidentAssigned) |  |  |
| incident_escalated | [IncidentEscalated](#psim-incident-v1-IncidentEscalated) |  |  |
| incident_comment_added | [IncidentCommentAdded](#psim-incident-v1-IncidentCommentAdded) |  |  |
| incident_link_added | [IncidentLinkAdded](#psim-incident-v1-IncidentLinkAdded) |  |  |
| incident_resolved | [IncidentResolved](#psim-incident-v1-IncidentResolved) |  |  |
| incident_reopened | [IncidentReopened](#psim-incident-v1-IncidentReopened) |  |  |
| incident_closed | [IncidentClosed](#psim-incident-v1-IncidentClosed) |  |  |
| incident_blocking_steps_changed | [IncidentBlockingStepsChanged](#psim-incident-v1-IncidentBlockingStepsChanged) |  |  |
| incident_transfer_requested | [IncidentTransferRequested](#psim-incident-v1-IncidentTransferRequested) |  |  |
| incident_transfer_confirmed | [IncidentTransferConfirmed](#psim-incident-v1-IncidentTransferConfirmed) |  |  |
| incident_command_updated | [IncidentCommandUpdated](#psim-incident-v1-IncidentCommandUpdated) |  |  |
| incident_notification_updated | [IncidentNotificationUpdated](#psim-incident-v1-IncidentNotificationUpdated) |  |  |
| incident_operational_updated | [IncidentOperationalUpdated](#psim-incident-v1-IncidentOperationalUpdated) |  |  |






<a name="psim-incident-v1-IncidentLinkAdded"></a>

### IncidentLinkAdded
IncidentLinkAdded публикуется при добавлении внешней ссылки.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| link | [Link](#psim-incident-v1-Link) |  |  |






<a name="psim-incident-v1-IncidentNotificationUpdated"></a>

### IncidentNotificationUpdated
IncidentNotificationUpdated добавляет квитанцию уведомления или состояние доставки.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| entry | [TimelineEntry](#psim-incident-v1-TimelineEntry) |  |  |






<a name="psim-incident-v1-IncidentOperationalUpdated"></a>

### IncidentOperationalUpdated
IncidentOperationalUpdated содержит полное состояние для проекций и replay после переподключения.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| action | [string](#string) |  |  |






<a name="psim-incident-v1-IncidentPriorityChanged"></a>

### IncidentPriorityChanged
IncidentPriorityChanged публикуется при вычисленном или ручном изменении приоритета.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| previous_priority | [psim.common.v1.Priority](#psim-common-v1-Priority) |  |  |






<a name="psim-incident-v1-IncidentReopened"></a>

### IncidentReopened
IncidentReopened публикуется при возврате из resolved в in_progress.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| reason | [string](#string) |  |  |






<a name="psim-incident-v1-IncidentResolved"></a>

### IncidentResolved
IncidentResolved публикуется при переходе в resolved.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-incident-v1-IncidentTransferConfirmed"></a>

### IncidentTransferConfirmed
IncidentTransferConfirmed меняет ответственного после подтверждения получателем.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| previous_assignee_id | [string](#string) |  |  |
| transfer_id | [string](#string) |  |  |






<a name="psim-incident-v1-IncidentTransferRequested"></a>

### IncidentTransferRequested
IncidentTransferRequested сохраняет текущего ответственного.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-incident-v1-IncidentWorkStarted"></a>

### IncidentWorkStarted
IncidentWorkStarted публикуется при переходе в in_progress.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-incident-v1-SignalAttached"></a>

### SignalAttached
SignalAttached публикуется при присоединении сигнала к открытому инциденту.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| signal_id | [string](#string) |  |  |















<a name="psim_ingest_v1_raw_event-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/ingest/v1/raw_event.proto



<a name="psim-ingest-v1-RawEvent"></a>

### RawEvent
RawEvent - событие в виде, полученном от коннектора, до нормализации.
envelope.message_id - event_id, назначенный Connector SDK,
envelope.sequence - порядковый номер источника, envelope.occurred_at - время события.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| connector_id | [string](#string) |  |  |
| source_id | [string](#string) |  | Детерминированный идентификатор UUIDv5(connector_id, source_ref), см. ADR-029. |
| source_ref | [string](#string) |  | Ссылка источника, известная коннектору, Source.external_ref в каталоге. |
| source_epoch | [uint32](#uint32) |  | Увеличивается, когда источник сбрасывает счётчик последовательности. |
| raw_code | [string](#string) |  | Код события производителя. |
| attributes | [RawEvent.AttributesEntry](#psim-ingest-v1-RawEvent-AttributesEntry) | repeated |  |
| clock_skew_corrected | [bool](#bool) |  | Задаётся, когда occurred_at заменено на received_at из-за рассогласования часов (ADR-010). |
| source_occurred_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  | Исходная метка времени источника при установленном clock_skew_corrected. |
| live_observation | [bool](#bool) |  | True только для свежего наблюдения, полученного в аутентифицированной сессии шлюза. |
| observation_session_id | [string](#string) |  |  |
| gateway_session_id | [string](#string) |  | Аутентифицированная сессия приёма, в том числе для исторического replay. |






<a name="psim-ingest-v1-RawEvent-AttributesEntry"></a>

### RawEvent.AttributesEntry



| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-ingest-v1-RawEventRecord"></a>

### RawEventRecord
RawEventRecord - значение топика psim.ingest.raw.v1 с ключом source_id.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| raw_event | [RawEvent](#psim-ingest-v1-RawEvent) |  |  |















<a name="psim_ingest_v1_state-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/ingest/v1/state.proto



<a name="psim-ingest-v1-ConnectorConnection"></a>

### ConnectorConnection
ConnectorConnection никогда не означает наличие текущего наблюдения его оборудования.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| online | [bool](#bool) |  |  |
| reason | [string](#string) |  |  |
| quality | [string](#string) |  | Новая сессия всегда начинается с неизвестного качества данных оборудования. |






<a name="psim-ingest-v1-GatewayStateRecord"></a>

### GatewayStateRecord
GatewayStateRecord содержит аутентифицированные наблюдения соединения и оборудования.
Топик psim.ingest.state.v1 имеет ключ connector_id; курсоры источников упорядочивают обновления оборудования.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| connector_id | [string](#string) |  |  |
| session_id | [string](#string) |  |  |
| connection | [ConnectorConnection](#psim-ingest-v1-ConnectorConnection) |  |  |
| state_report | [psim.connector.v1.StateReport](#psim-connector-v1-StateReport) |  |  |















<a name="psim_processing_v1_internal-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/processing/v1/internal.proto



<a name="psim-processing-v1-CorrelationChangelogRecord"></a>

### CorrelationChangelogRecord
CorrelationChangelogRecord - значение внутреннего компактируемого топика
psim.correlation.changelog.v1 с ключом rule_id:correlation_key. Не является публичным контрактом.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| state | [CorrelationStateEntry](#psim-processing-v1-CorrelationStateEntry) |  |  |






<a name="psim-processing-v1-CorrelationStateEntry"></a>

### CorrelationStateEntry
CorrelationStateEntry - непрозрачное состояние одного правила для одного ключа корреляции.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| rule_id | [string](#string) |  |  |
| rule_version | [uint32](#uint32) |  |  |
| correlation_key | [string](#string) |  |  |
| state | [bytes](#bytes) |  | Кодирование определяется движком и версионируется через state_format. |
| state_format | [uint32](#uint32) |  |  |






<a name="psim-processing-v1-LiveCandidate"></a>

### LiveCandidate
LiveCandidate сохраняет свидетельства и совпавшие части открытого окна.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| event | [EventRecord](#psim-processing-v1-EventRecord) |  |  |
| mask | [uint32](#uint32) |  |  |






<a name="psim-processing-v1-LiveKeyState"></a>

### LiveKeyState
LiveKeyState - версионированное кодирование ключа корреляции для локального состояния и changelog.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| candidates | [LiveCandidate](#psim-processing-v1-LiveCandidate) | repeated |  |
| pending | [LivePending](#psim-processing-v1-LivePending) | repeated |  |
| subject | [EventRecord](#psim-processing-v1-EventRecord) |  |  |
| next_deadline_ms | [int64](#int64) |  |  |
| last_signal_ms | [int64](#int64) | optional |  |
| seen | [bool](#bool) |  |  |
| buffered | [EventRecord](#psim-processing-v1-EventRecord) | repeated |  |
| watermark_ms | [int64](#int64) |  |  |
| sources | [LiveSource](#psim-processing-v1-LiveSource) | repeated |  |
| rule | [CorrelationRule](#psim-processing-v1-CorrelationRule) |  |  |
| rule_set_version | [uint64](#uint64) |  |  |
| last_activity_ms | [int64](#int64) |  |  |






<a name="psim-processing-v1-LivePending"></a>

### LivePending
LivePending сохраняет триггер absence до его срока по времени событий.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| trigger | [EventRecord](#psim-processing-v1-EventRecord) |  |  |
| deadline_ms | [int64](#int64) |  |  |






<a name="psim-processing-v1-LiveSource"></a>

### LiveSource
LiveSource отслеживает время событий и ограниченный курсор последовательности эпохи источника.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| source_id | [string](#string) |  |  |
| maximum_ms | [int64](#int64) |  |  |
| received_ms | [int64](#int64) |  | Время поступления в обработку, используемое только для определения простоя. |
| sequence | [SourceSequenceState](#psim-processing-v1-SourceSequenceState) |  |  |






<a name="psim-processing-v1-NormalizerStateRecord"></a>

### NormalizerStateRecord
NormalizerStateRecord - значение внутреннего компактируемого топика psim.normalizer.state.v1
Ключ: tenant_id/source_id:epoch. Не является публичным контрактом.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| source_state | [SourceSequenceState](#psim-processing-v1-SourceSequenceState) |  |  |






<a name="psim-processing-v1-RepartitionRecord"></a>

### RepartitionRecord
RepartitionRecord - значение внутреннего топика psim.correlation.repartition.v1
Ключ: correlation_key. Не является публичным контрактом.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| repartitioned_event | [RepartitionedEvent](#psim-processing-v1-RepartitionedEvent) |  |  |






<a name="psim-processing-v1-RepartitionedEvent"></a>

### RepartitionedEvent
RepartitionedEvent содержит одно неизменяемое правило и выбранное им событие.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| rule_id | [string](#string) |  |  |
| rule_version | [uint32](#uint32) |  |  |
| correlation_key | [string](#string) |  |  |
| event | [Event](#psim-processing-v1-Event) |  |  |
| rule | [CorrelationRule](#psim-processing-v1-CorrelationRule) |  | Неизменяемый скомпилированный вход; изменение конфигурации не переинтерпретирует события в очереди. |
| rule_set_version | [uint64](#uint64) |  |  |
| inventory | [bool](#bool) |  | Наблюдения инвентаризации инициализируют периодический absence без создания фиктивного события. |






<a name="psim-processing-v1-SourceSequenceState"></a>

### SourceSequenceState
SourceSequenceState отслеживает дедупликацию одной эпохи источника.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| source_id | [string](#string) |  |  |
| source_epoch | [uint32](#uint32) |  |  |
| max_sequence | [uint64](#uint64) |  |  |
| window | [bytes](#bytes) |  | Битовая карта последних 4096 номеров от max_sequence назад; установленный бит i означает, что max_sequence - i уже наблюдался. |
| last_confirmed_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |















<a name="psim_response_v1_events-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/response/v1/events.proto



<a name="psim-response-v1-CommandRequested"></a>

### CommandRequested
CommandRequested пересекает долговечную границу response -&gt; command. Только Response Engine
формирует эти запросы; критичность команды и авторизация устройства остаются под контролем сервера.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command | [Command](#psim-response-v1-Command) |  | requested_by сохраняет проверенного пользователя для ручных шагов и сервис для автоматических. |
| site_id | [string](#string) |  |  |
| zone_id | [string](#string) |  |  |






<a name="psim-response-v1-CommandUpdated"></a>

### CommandUpdated
CommandUpdated сообщает изменение команды; изменение равно новому состоянию.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| command | [Command](#psim-response-v1-Command) |  |  |






<a name="psim-response-v1-EscalationLevelTriggered"></a>

### EscalationLevelTriggered
EscalationLevelTriggered сообщает срабатывание уровня эскалации инцидента.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| level | [uint32](#uint32) |  |  |
| policy_id | [string](#string) |  |  |
| notification_ids | [string](#string) | repeated |  |
| sla_kind | [string](#string) |  | Независимое измерение SLA, вызвавшее этот уровень. |






<a name="psim-response-v1-NotificationRequested"></a>

### NotificationRequested
NotificationRequested фиксирует намерение; доставкой по каналу владеет Notification Service.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| request_id | [string](#string) |  |  |
| incident_id | [string](#string) |  |  |
| run_id | [string](#string) |  |  |
| step_id | [string](#string) |  |  |
| params | [NotifyParams](#psim-response-v1-NotifyParams) |  |  |
| site_id | [string](#string) |  |  |
| zone_id | [string](#string) |  |  |






<a name="psim-response-v1-NotificationUpdated"></a>

### NotificationUpdated
NotificationUpdated сообщает изменение уведомления; изменение равно новому состоянию.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| notification | [Notification](#psim-response-v1-Notification) |  |  |






<a name="psim-response-v1-PlanUnavailable"></a>

### PlanUnavailable
PlanUnavailable сообщает, что ни один опубликованный сценарий не подошёл новому инциденту.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |






<a name="psim-response-v1-ResponseEventRecord"></a>

### ResponseEventRecord
ResponseEventRecord - значение топика psim.response.events.v1
Ключ: incident_id; command_id или notification_id при отсутствии инцидента.
События содержат состояние агрегата после изменения; envelope.sequence = версия агрегата.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| run_updated | [RunUpdated](#psim-response-v1-RunUpdated) |  |  |
| command_updated | [CommandUpdated](#psim-response-v1-CommandUpdated) |  |  |
| notification_updated | [NotificationUpdated](#psim-response-v1-NotificationUpdated) |  |  |
| sla_breached | [SlaBreached](#psim-response-v1-SlaBreached) |  |  |
| escalation_level_triggered | [EscalationLevelTriggered](#psim-response-v1-EscalationLevelTriggered) |  |  |
| command_requested | [CommandRequested](#psim-response-v1-CommandRequested) |  |  |
| notification_requested | [NotificationRequested](#psim-response-v1-NotificationRequested) |  |  |
| plan_unavailable | [PlanUnavailable](#psim-response-v1-PlanUnavailable) |  |  |
| sla_policy_pinned | [SlaPolicyPinned](#psim-response-v1-SlaPolicyPinned) |  |  |
| site_duty_changed | [SiteDutyChanged](#psim-response-v1-SiteDutyChanged) |  |  |






<a name="psim-response-v1-RunUpdated"></a>

### RunUpdated
RunUpdated сообщает изменение запуска сценария реагирования.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| change | [RunChange](#psim-response-v1-RunChange) |  |  |
| step_id | [string](#string) |  | Шаг, затронутый изменением, если есть. |
| run | [ResponseRun](#psim-response-v1-ResponseRun) |  |  |






<a name="psim-response-v1-SiteDutyChanged"></a>

### SiteDutyChanged
SiteDutyChanged публикует текущего ответственного за площадку после фиксации замены.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| site_id | [string](#string) |  |  |
| user_id | [string](#string) |  |  |
| fallback_user_id | [string](#string) |  |  |
| version | [uint64](#uint64) |  |  |
| updated_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-response-v1-SlaBreached"></a>

### SlaBreached
SlaBreached сообщает о нарушении SLA инцидента.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| sla_kind | [string](#string) |  | &quot;ack&quot; или &quot;resolve&quot;. |
| policy_id | [string](#string) |  |  |






<a name="psim-response-v1-SlaPolicyPinned"></a>

### SlaPolicyPinned
SlaPolicyPinned сохраняет исходные цели SLA для аналитических проекций.
Публикуется атомарно с первым отслеживаемым инцидентом, независимо от последующих правок политики.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| created_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| acknowledge_within | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| resolve_within | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |








<a name="psim-response-v1-RunChange"></a>

### RunChange
RunChange соответствует доменным событиям ResponseRunStarted, StepActivated, StepCompleted, StepSkipped,
StepFailed, MandatoryStepsChanged, ResponseRunCompleted, ResponseRunCancelled.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| RUN_CHANGE_UNSPECIFIED | 0 |  |
| RUN_CHANGE_RUN_STARTED | 1 |  |
| RUN_CHANGE_STEP_ACTIVATED | 2 |  |
| RUN_CHANGE_STEP_COMPLETED | 3 |  |
| RUN_CHANGE_STEP_SKIPPED | 4 |  |
| RUN_CHANGE_STEP_FAILED | 5 |  |
| RUN_CHANGE_MANDATORY_STEPS_CHANGED | 6 |  |
| RUN_CHANGE_RUN_COMPLETED | 7 |  |
| RUN_CHANGE_RUN_CANCELLED | 8 |  |










<a name="psim_realtime_v1_realtime-proto"></a>
<p align="right"><a href="#top">К началу</a></p>

## psim/realtime/v1/realtime.proto



<a name="psim-realtime-v1-Authenticate"></a>

### Authenticate
Authenticate аутентифицирует соединение; также отправляется при обновлении токена.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| access_token | [string](#string) |  |  |
| supported_change_types | [string](#string) | repeated | Пустое значение означает прежнюю возможность incident_event. |






<a name="psim-realtime-v1-Authenticated"></a>

### Authenticated
Authenticated подтверждает аутентификацию.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| user_id | [string](#string) |  |  |
| expires_in | [uint32](#uint32) |  | Секунды до истечения токена; клиент должен повторно аутентифицироваться раньше. |
| supported_streams | [Stream](#psim-realtime-v1-Stream) | repeated |  |
| supported_change_types | [string](#string) | repeated |  |






<a name="psim-realtime-v1-ClientFrame"></a>

### ClientFrame
ClientFrame - кадр, отправленный браузером.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| request_id | [string](#string) |  | Идентификатор, выбранный клиентом и повторяемый в ответах. |
| authenticate | [Authenticate](#psim-realtime-v1-Authenticate) |  |  |
| subscribe | [Subscribe](#psim-realtime-v1-Subscribe) |  |  |
| unsubscribe | [Unsubscribe](#psim-realtime-v1-Unsubscribe) |  |  |
| ping | [Ping](#psim-realtime-v1-Ping) |  |  |






<a name="psim-realtime-v1-Delta"></a>

### Delta
Delta - одно изменение; применять только при версии агрегата новее известной.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| subscription_id | [string](#string) |  |  |
| resume_token | [string](#string) |  | Токен для продолжения после этой delta. |
| incident_event | [psim.incident.v1.IncidentEventRecord](#psim-incident-v1-IncidentEventRecord) |  |  |
| response_event | [psim.response.v1.ResponseEventRecord](#psim-response-v1-ResponseEventRecord) |  |  |
| location_status | [psim.api.v1.GetLocationStatusResponse](#psim-api-v1-GetLocationStatusResponse) |  | Заменить полное состояние локации, включая удалённые ресурсы и истёкшую актуальность данных. |






<a name="psim-realtime-v1-ErrorFrame"></a>

### ErrorFrame
ErrorFrame сообщает ошибку; после фатальных ошибок соединение закрывается.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| code | [string](#string) |  | Код ошибки из каталога, например &quot;REALTIME_SLOW_CONSUMER&quot;. |
| message | [string](#string) |  |  |
| fatal | [bool](#bool) |  |  |






<a name="psim-realtime-v1-FeedFilter"></a>

### FeedFilter
FeedFilter сужает подписку; сервер всегда применяет область доступа.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| site_ids | [string](#string) | repeated |  |
| zone_ids | [string](#string) | repeated |  |
| incident_id | [string](#string) |  |  |
| include_closed | [bool](#bool) |  |  |
| location_id | [string](#string) |  |  |






<a name="psim-realtime-v1-Ping"></a>

### Ping
Ping проверяет соединение.






<a name="psim-realtime-v1-Pong"></a>

### Pong
Pong отвечает на Ping.






<a name="psim-realtime-v1-ResyncRequired"></a>

### ResyncRequired
ResyncRequired требует новый snapshot при слишком большом разрыве или истёкшем токене.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| subscription_id | [string](#string) |  |  |
| reason | [string](#string) |  |  |






<a name="psim-realtime-v1-ServerFrame"></a>

### ServerFrame
ServerFrame - кадр, отправленный сервером.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| request_id | [string](#string) |  |  |
| authenticated | [Authenticated](#psim-realtime-v1-Authenticated) |  |  |
| subscribed | [Subscribed](#psim-realtime-v1-Subscribed) |  |  |
| delta | [Delta](#psim-realtime-v1-Delta) |  |  |
| resync_required | [ResyncRequired](#psim-realtime-v1-ResyncRequired) |  |  |
| error | [ErrorFrame](#psim-realtime-v1-ErrorFrame) |  |  |
| pong | [Pong](#psim-realtime-v1-Pong) |  |  |






<a name="psim-realtime-v1-Subscribe"></a>

### Subscribe
Subscribe открывает подписку с resume token.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| subscription_id | [string](#string) |  |  |
| stream | [Stream](#psim-realtime-v1-Stream) |  |  |
| filter | [FeedFilter](#psim-realtime-v1-FeedFilter) |  |  |
| resume_token | [string](#string) |  | Токен snapshot из GET /v1/feed/incidents или последней полученной Delta. |






<a name="psim-realtime-v1-Subscribed"></a>

### Subscribed
Subscribed подтверждает подписку.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| subscription_id | [string](#string) |  |  |
| resume_token | [string](#string) |  |  |






<a name="psim-realtime-v1-Unsubscribe"></a>

### Unsubscribe
Unsubscribe закрывает подписку.


| Поле | Тип | Метка | Описание |
| ----- | ---- | ----- | ----------- |
| subscription_id | [string](#string) |  |  |








<a name="psim-realtime-v1-Stream"></a>

### Stream
Stream выбирает получаемые данные.

| Имя | Номер | Описание |
| ---- | ------ | ----------- |
| STREAM_UNSPECIFIED | 0 |  |
| STREAM_INCIDENTS | 1 | Изменения инцидентов и реагирования в области доступа пользователя. |
| STREAM_INCIDENT | 2 | Изменения одного инцидента; требуется filter.incident_id. |
| STREAM_LOCATION_STATUS | 3 | Текущее состояние локации из авторитетного источника; промежуточные состояния могут объединяться. |










<a name="scalar-value-types"></a>
## Скалярные типы

| Тип .proto | Примечания | C++ | Java | Python | Go | C# | PHP | Ruby |
| ----------- | ----- | --- | ---- | ------ | -- | -- | --- | ---- |
| <a name="double" /> double |  | double | double | float | float64 | double | float | Float |
| <a name="float" /> float |  | float | float | float | float32 | float | float | Float |
| <a name="int32" /> int32 | Использует кодирование переменной длины. Неэффективно для отрицательных чисел; если они возможны, используйте sint32. | int32 | int | int | int32 | int | integer | Bignum или Fixnum (по необходимости) |
| <a name="int64" /> int64 | Использует кодирование переменной длины. Неэффективно для отрицательных чисел; если они возможны, используйте sint64. | int64 | long | int/long | int64 | long | integer/string | Bignum |
| <a name="uint32" /> uint32 | Использует кодирование переменной длины. | uint32 | int | int/long | uint32 | uint | integer | Bignum или Fixnum (по необходимости) |
| <a name="uint64" /> uint64 | Использует кодирование переменной длины. | uint64 | long | int/long | uint64 | ulong | integer/string | Bignum или Fixnum (по необходимости) |
| <a name="sint32" /> sint32 | Использует кодирование переменной длины. Знаковое целое; отрицательные числа кодируются эффективнее, чем обычным int32. | int32 | int | int | int32 | int | integer | Bignum или Fixnum (по необходимости) |
| <a name="sint64" /> sint64 | Использует кодирование переменной длины. Знаковое целое; отрицательные числа кодируются эффективнее, чем обычным int64. | int64 | long | int/long | int64 | long | integer/string | Bignum |
| <a name="fixed32" /> fixed32 | Всегда четыре байта. Эффективнее uint32, если значения часто превышают 2^28. | uint32 | int | int | uint32 | uint | integer | Bignum или Fixnum (по необходимости) |
| <a name="fixed64" /> fixed64 | Всегда восемь байтов. Эффективнее uint64, если значения часто превышают 2^56. | uint64 | long | int/long | uint64 | ulong | integer/string | Bignum |
| <a name="sfixed32" /> sfixed32 | Всегда четыре байта. | int32 | int | int | int32 | int | integer | Bignum или Fixnum (по необходимости) |
| <a name="sfixed64" /> sfixed64 | Всегда восемь байтов. | int64 | long | int/long | int64 | long | integer/string | Bignum |
| <a name="bool" /> bool |  | bool | boolean | boolean | bool | bool | boolean | TrueClass/FalseClass |
| <a name="string" /> string | Строка должна содержать текст UTF-8 или 7-битный ASCII. | string | String | str/unicode | string | string | string | String (UTF-8) |
| <a name="bytes" /> bytes | Может содержать произвольную последовательность байтов. | string | ByteString | str | []byte | ByteString | string | String (ASCII-8BIT) |

<!-- imported-types -->
<a name="imported-message-types"></a>
## Импортированные типы сообщений

Спецификации: [типы Protobuf](https://protobuf.dev/reference/protobuf/google.protobuf/), [тело HTTP](https://github.com/googleapis/googleapis/blob/master/google/api/httpbody.proto).

<a name="google-protobuf-Timestamp"></a>
### google.protobuf.Timestamp

| Поле | Тип | Метка | Описание |
| --- | --- | --- | --- |
| seconds | [int64](#int64) | | Секунды UTC относительно эпохи Unix. |
| nanos | [int32](#int32) | | Дробная часть секунды в наносекундах, от 0 до 999999999. |

<a name="google-protobuf-Duration"></a>
### google.protobuf.Duration

| Поле | Тип | Метка | Описание |
| --- | --- | --- | --- |
| seconds | [int64](#int64) | | Целые секунды со знаком. |
| nanos | [int32](#int32) | | Дробная часть секунды в наносекундах; её знак совпадает со знаком ненулевых секунд. |

<a name="google-protobuf-Any"></a>
### google.protobuf.Any

| Поле | Тип | Метка | Описание |
| --- | --- | --- | --- |
| type_url | [string](#string) | | URL, определяющий полное имя типа сообщения. |
| value | [bytes](#bytes) | | Сериализованные байты сообщения указанного типа. |

<a name="google-api-HttpBody"></a>
### google.api.HttpBody

| Поле | Тип | Метка | Описание |
| --- | --- | --- | --- |
| content_type | [string](#string) | | MIME-тип тела HTTP. |
| data | [bytes](#bytes) | | Исходное содержимое запроса или ответа. |
| extensions | [google.protobuf.Any](#google-protobuf-Any) | repeated | Дополнительные метаданные ответа; потоковые API передают их в первом ответе. |
