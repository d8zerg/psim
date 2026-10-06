# Protocol Documentation
<a name="top"></a>

## Table of Contents

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
  
    - [LocationKind](#psim-catalog-v1-LocationKind)
    - [ParameterType](#psim-catalog-v1-ParameterType)
    - [ResourceState](#psim-catalog-v1-ResourceState)
  
- [psim/api/v1/catalog.proto](#psim_api_v1_catalog-proto)
    - [ArchiveLocationRequest](#psim-api-v1-ArchiveLocationRequest)
    - [ArchiveLocationResponse](#psim-api-v1-ArchiveLocationResponse)
    - [ConnectorStatus](#psim-api-v1-ConnectorStatus)
    - [CreateDeviceRequest](#psim-api-v1-CreateDeviceRequest)
    - [CreateDeviceResponse](#psim-api-v1-CreateDeviceResponse)
    - [CreateDeviceTypeRequest](#psim-api-v1-CreateDeviceTypeRequest)
    - [CreateDeviceTypeResponse](#psim-api-v1-CreateDeviceTypeResponse)
    - [CreateLocationRequest](#psim-api-v1-CreateLocationRequest)
    - [CreateLocationResponse](#psim-api-v1-CreateLocationResponse)
    - [ExportDevicesRequest](#psim-api-v1-ExportDevicesRequest)
    - [ExportDevicesResponse](#psim-api-v1-ExportDevicesResponse)
    - [GetDeviceRequest](#psim-api-v1-GetDeviceRequest)
    - [GetDeviceResponse](#psim-api-v1-GetDeviceResponse)
    - [GetFloorPlanRequest](#psim-api-v1-GetFloorPlanRequest)
    - [GetFloorPlanResponse](#psim-api-v1-GetFloorPlanResponse)
    - [GetImportRequest](#psim-api-v1-GetImportRequest)
    - [GetImportResponse](#psim-api-v1-GetImportResponse)
    - [GetLocationRequest](#psim-api-v1-GetLocationRequest)
    - [GetLocationResponse](#psim-api-v1-GetLocationResponse)
    - [ImportJob](#psim-api-v1-ImportJob)
    - [ImportRowError](#psim-api-v1-ImportRowError)
    - [ListConnectorsRequest](#psim-api-v1-ListConnectorsRequest)
    - [ListConnectorsResponse](#psim-api-v1-ListConnectorsResponse)
    - [ListDeviceTypesRequest](#psim-api-v1-ListDeviceTypesRequest)
    - [ListDeviceTypesResponse](#psim-api-v1-ListDeviceTypesResponse)
    - [ListDevicesRequest](#psim-api-v1-ListDevicesRequest)
    - [ListDevicesResponse](#psim-api-v1-ListDevicesResponse)
    - [ListLocationsRequest](#psim-api-v1-ListLocationsRequest)
    - [ListLocationsResponse](#psim-api-v1-ListLocationsResponse)
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
    - [SearchEventsRequest](#psim-api-v1-SearchEventsRequest)
    - [SearchEventsResponse](#psim-api-v1-SearchEventsResponse)
    - [StoredEvent](#psim-api-v1-StoredEvent)
  
    - [EventHistoryService](#psim-api-v1-EventHistoryService)
  
- [psim/incident/v1/incident.proto](#psim_incident_v1_incident-proto)
    - [Incident](#psim-incident-v1-Incident)
    - [IncidentType](#psim-incident-v1-IncidentType)
    - [Link](#psim-incident-v1-Link)
    - [StepRef](#psim-incident-v1-StepRef)
    - [TimelineEntry](#psim-incident-v1-TimelineEntry)
    - [TimelineEntry.DetailsEntry](#psim-incident-v1-TimelineEntry-DetailsEntry)
  
    - [IncidentState](#psim-incident-v1-IncidentState)
    - [PrioritySource](#psim-incident-v1-PrioritySource)
    - [Resolution](#psim-incident-v1-Resolution)
    - [TimelineEntryKind](#psim-incident-v1-TimelineEntryKind)
  
- [psim/api/v1/feed.proto](#psim_api_v1_feed-proto)
    - [DeviceStatus](#psim-api-v1-DeviceStatus)
    - [GetIncidentFeedRequest](#psim-api-v1-GetIncidentFeedRequest)
    - [GetIncidentFeedResponse](#psim-api-v1-GetIncidentFeedResponse)
    - [GetLocationStatusRequest](#psim-api-v1-GetLocationStatusRequest)
    - [GetLocationStatusResponse](#psim-api-v1-GetLocationStatusResponse)
    - [GetOperatorLoadRequest](#psim-api-v1-GetOperatorLoadRequest)
    - [GetOperatorLoadResponse](#psim-api-v1-GetOperatorLoadResponse)
    - [OperatorLoad](#psim-api-v1-OperatorLoad)
    - [ZoneStatus](#psim-api-v1-ZoneStatus)
  
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
    - [UpdateIncidentTypeRequest](#psim-api-v1-UpdateIncidentTypeRequest)
    - [UpdateIncidentTypeResponse](#psim-api-v1-UpdateIncidentTypeResponse)
  
    - [IncidentService](#psim-api-v1-IncidentService)
  
- [psim/api/v1/me.proto](#psim_api_v1_me-proto)
    - [GetMeRequest](#psim-api-v1-GetMeRequest)
    - [GetMeResponse](#psim-api-v1-GetMeResponse)
  
    - [MeService](#psim-api-v1-MeService)
  
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
  
- [psim/response/v1/notification.proto](#psim_response_v1_notification-proto)
    - [Notification](#psim-response-v1-Notification)
    - [Notification.DataEntry](#psim-response-v1-Notification-DataEntry)
  
    - [NotificationState](#psim-response-v1-NotificationState)
  
- [psim/api/v1/notifications.proto](#psim_api_v1_notifications-proto)
    - [ListMyNotificationsRequest](#psim-api-v1-ListMyNotificationsRequest)
    - [ListMyNotificationsResponse](#psim-api-v1-ListMyNotificationsResponse)
    - [MarkNotificationReadRequest](#psim-api-v1-MarkNotificationReadRequest)
    - [MarkNotificationReadResponse](#psim-api-v1-MarkNotificationReadResponse)
  
    - [NotificationService](#psim-api-v1-NotificationService)
  
- [psim/api/v1/problem.proto](#psim_api_v1_problem-proto)
    - [FieldViolation](#psim-api-v1-FieldViolation)
    - [Problem](#psim-api-v1-Problem)
  
- [psim/api/v1/reports.proto](#psim_api_v1_reports-proto)
    - [ExportIncidentReportRequest](#psim-api-v1-ExportIncidentReportRequest)
    - [ExportIncidentReportResponse](#psim-api-v1-ExportIncidentReportResponse)
    - [GetIncidentReportRequest](#psim-api-v1-GetIncidentReportRequest)
    - [GetIncidentReportResponse](#psim-api-v1-GetIncidentReportResponse)
    - [IncidentMetrics](#psim-api-v1-IncidentMetrics)
    - [ReportFilter](#psim-api-v1-ReportFilter)
  
    - [ReportGrouping](#psim-api-v1-ReportGrouping)
  
    - [ReportService](#psim-api-v1-ReportService)
  
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
    - [SkipStepRequest](#psim-api-v1-SkipStepRequest)
    - [SkipStepResponse](#psim-api-v1-SkipStepResponse)
    - [UpdateEscalationPolicyRequest](#psim-api-v1-UpdateEscalationPolicyRequest)
    - [UpdateEscalationPolicyResponse](#psim-api-v1-UpdateEscalationPolicyResponse)
    - [ValidatePlanRequest](#psim-api-v1-ValidatePlanRequest)
    - [ValidatePlanResponse](#psim-api-v1-ValidatePlanResponse)
  
    - [EscalationPolicyService](#psim-api-v1-EscalationPolicyService)
    - [ResponsePlanService](#psim-api-v1-ResponsePlanService)
    - [ResponseRunService](#psim-api-v1-ResponseRunService)
  
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
    - [PublishMappingRuleSetRequest](#psim-api-v1-PublishMappingRuleSetRequest)
    - [PublishMappingRuleSetResponse](#psim-api-v1-PublishMappingRuleSetResponse)
    - [RuleVersionRef](#psim-api-v1-RuleVersionRef)
    - [SaveRuleDraftRequest](#psim-api-v1-SaveRuleDraftRequest)
    - [SaveRuleDraftResponse](#psim-api-v1-SaveRuleDraftResponse)
    - [StartDryRunRequest](#psim-api-v1-StartDryRunRequest)
    - [StartDryRunResponse](#psim-api-v1-StartDryRunResponse)
    - [ValidateRuleRequest](#psim-api-v1-ValidateRuleRequest)
    - [ValidateRuleResponse](#psim-api-v1-ValidateRuleResponse)
  
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
  
    - [GoodbyeReason](#psim-connector-v1-GoodbyeReason)
  
    - [ConnectorGatewayService](#psim-connector-v1-ConnectorGatewayService)
  
- [psim/incident/v1/events.proto](#psim_incident_v1_events-proto)
    - [IncidentAcknowledged](#psim-incident-v1-IncidentAcknowledged)
    - [IncidentAssigned](#psim-incident-v1-IncidentAssigned)
    - [IncidentBlockingStepsChanged](#psim-incident-v1-IncidentBlockingStepsChanged)
    - [IncidentClosed](#psim-incident-v1-IncidentClosed)
    - [IncidentCommentAdded](#psim-incident-v1-IncidentCommentAdded)
    - [IncidentCreated](#psim-incident-v1-IncidentCreated)
    - [IncidentEscalated](#psim-incident-v1-IncidentEscalated)
    - [IncidentEventRecord](#psim-incident-v1-IncidentEventRecord)
    - [IncidentLinkAdded](#psim-incident-v1-IncidentLinkAdded)
    - [IncidentPriorityChanged](#psim-incident-v1-IncidentPriorityChanged)
    - [IncidentReopened](#psim-incident-v1-IncidentReopened)
    - [IncidentResolved](#psim-incident-v1-IncidentResolved)
    - [IncidentWorkStarted](#psim-incident-v1-IncidentWorkStarted)
    - [SignalAttached](#psim-incident-v1-SignalAttached)
  
- [psim/ingest/v1/raw_event.proto](#psim_ingest_v1_raw_event-proto)
    - [RawEvent](#psim-ingest-v1-RawEvent)
    - [RawEvent.AttributesEntry](#psim-ingest-v1-RawEvent-AttributesEntry)
    - [RawEventRecord](#psim-ingest-v1-RawEventRecord)
  
- [psim/processing/v1/internal.proto](#psim_processing_v1_internal-proto)
    - [CorrelationChangelogRecord](#psim-processing-v1-CorrelationChangelogRecord)
    - [CorrelationStateEntry](#psim-processing-v1-CorrelationStateEntry)
    - [NormalizerStateRecord](#psim-processing-v1-NormalizerStateRecord)
    - [RepartitionRecord](#psim-processing-v1-RepartitionRecord)
    - [RepartitionedEvent](#psim-processing-v1-RepartitionedEvent)
    - [SourceSequenceState](#psim-processing-v1-SourceSequenceState)
  
- [psim/response/v1/events.proto](#psim_response_v1_events-proto)
    - [CommandUpdated](#psim-response-v1-CommandUpdated)
    - [EscalationLevelTriggered](#psim-response-v1-EscalationLevelTriggered)
    - [NotificationUpdated](#psim-response-v1-NotificationUpdated)
    - [ResponseEventRecord](#psim-response-v1-ResponseEventRecord)
    - [RunUpdated](#psim-response-v1-RunUpdated)
    - [SlaBreached](#psim-response-v1-SlaBreached)
  
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
  
- [Scalar Value Types](#scalar-value-types)



<a name="psim_common_v1_envelope-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/common/v1/envelope.proto



<a name="psim-common-v1-Actor"></a>

### Actor
Actor identifies the subject that performed an action.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| kind | [ActorKind](#psim-common-v1-ActorKind) |  |  |
| id | [string](#string) |  | User id, service name, connector id or &#34;system&#34;. |
| display_name | [string](#string) |  |  |






<a name="psim-common-v1-Envelope"></a>

### Envelope
Envelope carries the metadata shared by every message on every PSIM topic.
See ADR-005. The payload type is defined by the oneof of the topic record (ADR-004).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| message_id | [string](#string) |  | Message identifier: event_id, signal_id or domain event id (UUIDv7 or UUIDv5). |
| tenant_id | [string](#string) |  |  |
| occurred_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  | Time the fact happened in the domain (event time). |
| produced_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  | Time the message was written to Kafka. |
| received_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  | Time the platform received the event (ingest events only). |
| sequence | [uint64](#uint64) |  | Source sequence number for events, aggregate version for domain events. |
| causation_id | [string](#string) |  | Message that caused this one. |
| correlation_id | [string](#string) |  | Identifier of the originating event of the whole causal chain. |
| actor | [Actor](#psim-common-v1-Actor) |  |  |
| schema_version | [string](#string) |  | Version of the contracts release that produced the message, for diagnostics. |





 


<a name="psim-common-v1-ActorKind"></a>

### ActorKind
ActorKind is the kind of subject.

| Name | Number | Description |
| ---- | ------ | ----------- |
| ACTOR_KIND_UNSPECIFIED | 0 |  |
| ACTOR_KIND_USER | 1 |  |
| ACTOR_KIND_SERVICE | 2 |  |
| ACTOR_KIND_CONNECTOR | 3 |  |
| ACTOR_KIND_SYSTEM | 4 |  |


 

 

 



<a name="psim_audit_v1_audit-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/audit/v1/audit.proto



<a name="psim-audit-v1-AuditChainEntry"></a>

### AuditChainEntry
AuditChainEntry is an audit entry as stored in the chain.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| audit_seq | [uint64](#uint64) |  |  |
| source_event_id | [string](#string) |  |  |
| entry | [AuditEntry](#psim-audit-v1-AuditEntry) |  |  |
| occurred_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| recorded_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| prev_hash | [bytes](#bytes) |  |  |
| hash | [bytes](#bytes) |  |  |






<a name="psim-audit-v1-AuditCheckpoint"></a>

### AuditCheckpoint
AuditCheckpoint fixes the head of the chain (ADR-021).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| audit_seq | [uint64](#uint64) |  |  |
| hash | [bytes](#bytes) |  |  |
| created_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| signature | [bytes](#bytes) |  |  |
| key_id | [string](#string) |  |  |






<a name="psim-audit-v1-AuditEntry"></a>

### AuditEntry
AuditEntry is an action to be recorded in the audit chain.
envelope.causation_id is the source domain event id (idempotency key, AU3).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| actor | [psim.common.v1.Actor](#psim-common-v1-Actor) |  |  |
| action | [string](#string) |  | Dotted action name, for example &#34;incident.acknowledge&#34;, &#34;access.denied&#34;. |
| object_type | [string](#string) |  |  |
| object_id | [string](#string) |  |  |
| site_id | [string](#string) |  |  |
| outcome | [AuditOutcome](#psim-audit-v1-AuditOutcome) |  |  |
| details | [AuditEntry.DetailsEntry](#psim-audit-v1-AuditEntry-DetailsEntry) | repeated |  |
| client_address | [string](#string) |  | Client address and user agent for user actions. |
| user_agent | [string](#string) |  |  |






<a name="psim-audit-v1-AuditEntry-DetailsEntry"></a>

### AuditEntry.DetailsEntry



| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-audit-v1-AuditEventRecord"></a>

### AuditEventRecord
AuditEventRecord is the value of topic psim.audit.v1 (key: tenant_id).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| audit_entry | [AuditEntry](#psim-audit-v1-AuditEntry) |  |  |
| audit_checkpoint | [AuditCheckpoint](#psim-audit-v1-AuditCheckpoint) |  |  |





 


<a name="psim-audit-v1-AuditOutcome"></a>

### AuditOutcome
AuditOutcome is the result of the audited action.

| Name | Number | Description |
| ---- | ------ | ----------- |
| AUDIT_OUTCOME_UNSPECIFIED | 0 |  |
| AUDIT_OUTCOME_SUCCESS | 1 |  |
| AUDIT_OUTCOME_DENIED | 2 |  |
| AUDIT_OUTCOME_FAILED | 3 |  |


 

 

 



<a name="psim_api_v1_audit-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/api/v1/audit.proto



<a name="psim-api-v1-AuditQuery"></a>

### AuditQuery
AuditQuery selects audit records.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| from | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| to | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| actor_id | [string](#string) |  |  |
| action | [string](#string) |  | Action prefix, for example &#34;incident.&#34;. |
| object_type | [string](#string) |  |  |
| object_id | [string](#string) |  |  |
| site_id | [string](#string) |  |  |
| outcome | [psim.audit.v1.AuditOutcome](#psim-audit-v1-AuditOutcome) |  |  |






<a name="psim-api-v1-ExportAuditRecordsRequest"></a>

### ExportAuditRecordsRequest
ExportAuditRecordsRequest is the request of ExportAuditRecords.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| query | [AuditQuery](#psim-api-v1-AuditQuery) |  |  |






<a name="psim-api-v1-ExportAuditRecordsResponse"></a>

### ExportAuditRecordsResponse
ExportAuditRecordsResponse is the response of ExportAuditRecords; the body is JSON lines.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| file | [google.api.HttpBody](#google-api-HttpBody) |  |  |






<a name="psim-api-v1-SearchAuditRecordsRequest"></a>

### SearchAuditRecordsRequest
SearchAuditRecordsRequest is the request of SearchAuditRecords.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| query | [AuditQuery](#psim-api-v1-AuditQuery) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-SearchAuditRecordsResponse"></a>

### SearchAuditRecordsResponse
SearchAuditRecordsResponse is the response of SearchAuditRecords.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| records | [psim.audit.v1.AuditChainEntry](#psim-audit-v1-AuditChainEntry) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-VerifyAuditIntegrityRequest"></a>

### VerifyAuditIntegrityRequest
VerifyAuditIntegrityRequest is the request of VerifyAuditIntegrity.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| from_seq | [uint64](#uint64) |  | 0 means from the first record. |
| to_seq | [uint64](#uint64) |  | 0 means up to the last record. |






<a name="psim-api-v1-VerifyAuditIntegrityResponse"></a>

### VerifyAuditIntegrityResponse
VerifyAuditIntegrityResponse is the response of VerifyAuditIntegrity.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| intact | [bool](#bool) |  |  |
| records_checked | [uint64](#uint64) |  |  |
| first_broken_seq | [uint64](#uint64) |  | First record where the chain is broken; 0 when intact. |
| detail | [string](#string) |  |  |
| last_checkpoint | [psim.audit.v1.AuditCheckpoint](#psim-audit-v1-AuditCheckpoint) |  |  |





 

 

 


<a name="psim-api-v1-AuditService"></a>

### AuditService
AuditService searches and verifies the audit chain (ADR-021). Owner: Audit Service. Scenario S10.

| Method Name | Request Type | Response Type | Description |
| ----------- | ------------ | ------------- | ------------|
| SearchAuditRecords | [SearchAuditRecordsRequest](#psim-api-v1-SearchAuditRecordsRequest) | [SearchAuditRecordsResponse](#psim-api-v1-SearchAuditRecordsResponse) | SearchAuditRecords searches audit records; newest first. |
| VerifyAuditIntegrity | [VerifyAuditIntegrityRequest](#psim-api-v1-VerifyAuditIntegrityRequest) | [VerifyAuditIntegrityResponse](#psim-api-v1-VerifyAuditIntegrityResponse) | VerifyAuditIntegrity recomputes the chain over a range of records. |
| ExportAuditRecords | [ExportAuditRecordsRequest](#psim-api-v1-ExportAuditRecordsRequest) | [ExportAuditRecordsResponse](#psim-api-v1-ExportAuditRecordsResponse) | ExportAuditRecords exports records with neighbouring hashes and checkpoints (JSON lines). |

 



<a name="psim_common_v1_types-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/common/v1/types.proto



<a name="psim-common-v1-LocationRef"></a>

### LocationRef
LocationRef places an object in the site hierarchy.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| site_id | [string](#string) |  |  |
| zone_id | [string](#string) |  |  |






<a name="psim-common-v1-Position"></a>

### Position
Position is a point on a floor plan in plan units.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| x | [double](#double) |  |  |
| y | [double](#double) |  |  |





 


<a name="psim-common-v1-EventClass"></a>

### EventClass
EventClass is the behavioural class of a taxonomy type (event-taxonomy.md, section 2).

| Name | Number | Description |
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
Priority of a signal or an incident; P1 is the highest.

| Name | Number | Description |
| ---- | ------ | ----------- |
| PRIORITY_UNSPECIFIED | 0 |  |
| PRIORITY_P1 | 1 |  |
| PRIORITY_P2 | 2 |  |
| PRIORITY_P3 | 3 |  |
| PRIORITY_P4 | 4 |  |



<a name="psim-common-v1-Severity"></a>

### Severity
Severity of a single event (event-taxonomy.md, section 3).

| Name | Number | Description |
| ---- | ------ | ----------- |
| SEVERITY_UNSPECIFIED | 0 |  |
| SEVERITY_INFO | 1 |  |
| SEVERITY_LOW | 2 |  |
| SEVERITY_MEDIUM | 3 |  |
| SEVERITY_HIGH | 4 |  |
| SEVERITY_CRITICAL | 5 |  |


 

 

 



<a name="psim_catalog_v1_catalog-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/catalog/v1/catalog.proto



<a name="psim-catalog-v1-Capability"></a>

### Capability
Capability is a command or mode a device supports.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| code | [string](#string) |  | For example &#34;lock&#34;, &#34;unlock&#34;, &#34;arm&#34;, &#34;ptz.preset&#34;. |
| name | [string](#string) |  |  |
| critical | [bool](#bool) |  | Critical commands require approval by a second user (CM3). |
| parameters | [ParameterSpec](#psim-catalog-v1-ParameterSpec) | repeated |  |






<a name="psim-catalog-v1-CatalogRecord"></a>

### CatalogRecord
CatalogRecord is the value of compacted topic psim.catalog.v1
(key: resource_type:resource_id). Each record is the full current state of a resource.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| location | [Location](#psim-catalog-v1-Location) |  |  |
| device_type | [DeviceType](#psim-catalog-v1-DeviceType) |  |  |
| device | [Device](#psim-catalog-v1-Device) |  |  |
| connector | [Connector](#psim-catalog-v1-Connector) |  |  |






<a name="psim-catalog-v1-CertificateFingerprint"></a>

### CertificateFingerprint
CertificateFingerprint binds an mTLS client certificate to a connector.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| sha256 | [string](#string) |  | Hex-encoded SHA-256 of the DER certificate. |
| not_after | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-catalog-v1-Connector"></a>

### Connector
Connector is a registered connector with its credentials and limits (aggregates.md, 1.4).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| connector_id | [string](#string) |  |  |
| name | [string](#string) |  |  |
| connector_type | [string](#string) |  | Selects the mapping rule set, for example &#34;simulator&#34; or &#34;mqtt&#34;. |
| certificates | [CertificateFingerprint](#psim-catalog-v1-CertificateFingerprint) | repeated |  |
| limits | [ConnectorLimits](#psim-catalog-v1-ConnectorLimits) |  |  |
| state | [ResourceState](#psim-catalog-v1-ResourceState) |  |  |
| version | [uint64](#uint64) |  |  |






<a name="psim-catalog-v1-ConnectorLimits"></a>

### ConnectorLimits
ConnectorLimits are per-connector ingestion limits enforced by the gateway.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| max_events_per_second | [uint32](#uint32) |  |  |
| max_batch_events | [uint32](#uint32) |  |  |






<a name="psim-catalog-v1-Device"></a>

### Device
Device is a physical or logical device with its sources (aggregates.md, 1.3).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| device_id | [string](#string) |  |  |
| device_type_id | [string](#string) |  |  |
| zone_id | [string](#string) |  |  |
| site_id | [string](#string) |  |  |
| connector_id | [string](#string) |  |  |
| external_id | [string](#string) |  | Device id in the external system, unique per connector (D1). |
| name | [string](#string) |  |  |
| sources | [Source](#psim-catalog-v1-Source) | repeated |  |
| position | [psim.common.v1.Position](#psim-common-v1-Position) |  |  |
| state | [ResourceState](#psim-catalog-v1-ResourceState) |  |  |
| version | [uint64](#uint64) |  |  |
| updated_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-catalog-v1-DeviceType"></a>

### DeviceType
DeviceType describes a class of devices, its sources and capabilities (aggregates.md, 1.2).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| device_type_id | [string](#string) |  |  |
| code | [string](#string) |  |  |
| name | [string](#string) |  |  |
| taxonomy_domain | [string](#string) |  | Taxonomy domain, for example &#34;access&#34;. |
| source_templates | [SourceTemplate](#psim-catalog-v1-SourceTemplate) | repeated |  |
| capabilities | [Capability](#psim-catalog-v1-Capability) | repeated |  |
| state | [ResourceState](#psim-catalog-v1-ResourceState) |  |  |
| version | [uint64](#uint64) |  |  |






<a name="psim-catalog-v1-FloorPlan"></a>

### FloorPlan
FloorPlan is an image of a floor with a coordinate system for device positions.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| image_ref | [string](#string) |  | Reference to the stored image, served by GET /v1/locations/{id}/floor-plan. |
| content_type | [string](#string) |  |  |
| width | [double](#double) |  |  |
| height | [double](#double) |  |  |






<a name="psim-catalog-v1-Location"></a>

### Location
Location is a site, building, floor or zone (aggregates.md, 1.1).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| location_id | [string](#string) |  |  |
| kind | [LocationKind](#psim-catalog-v1-LocationKind) |  |  |
| parent_id | [string](#string) |  | Empty only for sites. |
| site_id | [string](#string) |  |  |
| name | [string](#string) |  |  |
| code | [string](#string) |  | Unique among siblings. |
| timezone | [string](#string) |  | IANA time zone, sites only. |
| floor_plan | [FloorPlan](#psim-catalog-v1-FloorPlan) |  | Floors only. |
| state | [ResourceState](#psim-catalog-v1-ResourceState) |  |  |
| version | [uint64](#uint64) |  |  |
| updated_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-catalog-v1-ParameterSpec"></a>

### ParameterSpec
ParameterSpec describes one command parameter.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| name | [string](#string) |  |  |
| type | [ParameterType](#psim-catalog-v1-ParameterType) |  |  |
| required | [bool](#bool) |  |  |
| allowed_values | [string](#string) | repeated |  |






<a name="psim-catalog-v1-Source"></a>

### Source
Source is an event channel of a device.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| source_id | [string](#string) |  | Deterministic: UUIDv5(connector_id, external_ref), see ADR-029. |
| external_ref | [string](#string) |  | Reference used by the connector, unique per connector (D2). |
| kind | [string](#string) |  |  |
| name | [string](#string) |  |  |






<a name="psim-catalog-v1-SourceTemplate"></a>

### SourceTemplate
SourceTemplate describes a source every device of the type has.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| kind | [string](#string) |  |  |
| name | [string](#string) |  |  |





 


<a name="psim-catalog-v1-LocationKind"></a>

### LocationKind
LocationKind is the level of a location in the hierarchy.

| Name | Number | Description |
| ---- | ------ | ----------- |
| LOCATION_KIND_UNSPECIFIED | 0 |  |
| LOCATION_KIND_SITE | 1 |  |
| LOCATION_KIND_BUILDING | 2 |  |
| LOCATION_KIND_FLOOR | 3 |  |
| LOCATION_KIND_ZONE | 4 |  |



<a name="psim-catalog-v1-ParameterType"></a>

### ParameterType
ParameterType is the type of a command parameter.

| Name | Number | Description |
| ---- | ------ | ----------- |
| PARAMETER_TYPE_UNSPECIFIED | 0 |  |
| PARAMETER_TYPE_STRING | 1 |  |
| PARAMETER_TYPE_INTEGER | 2 |  |
| PARAMETER_TYPE_NUMBER | 3 |  |
| PARAMETER_TYPE_BOOLEAN | 4 |  |
| PARAMETER_TYPE_DURATION | 5 |  |



<a name="psim-catalog-v1-ResourceState"></a>

### ResourceState
ResourceState is the lifecycle state shared by catalog resources.

| Name | Number | Description |
| ---- | ------ | ----------- |
| RESOURCE_STATE_UNSPECIFIED | 0 |  |
| RESOURCE_STATE_ACTIVE | 1 |  |
| RESOURCE_STATE_DISABLED | 2 |  |
| RESOURCE_STATE_ARCHIVED | 3 |  |


 

 

 



<a name="psim_api_v1_catalog-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/api/v1/catalog.proto



<a name="psim-api-v1-ArchiveLocationRequest"></a>

### ArchiveLocationRequest
ArchiveLocationRequest is the request of ArchiveLocation.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| location_id | [string](#string) |  |  |






<a name="psim-api-v1-ArchiveLocationResponse"></a>

### ArchiveLocationResponse
ArchiveLocationResponse is the response of ArchiveLocation.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| location | [psim.catalog.v1.Location](#psim-catalog-v1-Location) |  |  |






<a name="psim-api-v1-ConnectorStatus"></a>

### ConnectorStatus
ConnectorStatus is the runtime status of a connector, joined from the session registry.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| connector | [psim.catalog.v1.Connector](#psim-catalog-v1-Connector) |  |  |
| online | [bool](#bool) |  |  |
| last_seen_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| events_per_second | [double](#double) |  |  |
| rejected_events_total | [uint64](#uint64) |  |  |
| dlq_events_total | [uint64](#uint64) |  |  |






<a name="psim-api-v1-CreateDeviceRequest"></a>

### CreateDeviceRequest
CreateDeviceRequest is the request of CreateDevice.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| device | [psim.catalog.v1.Device](#psim-catalog-v1-Device) |  |  |






<a name="psim-api-v1-CreateDeviceResponse"></a>

### CreateDeviceResponse
CreateDeviceResponse is the response of CreateDevice.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| device | [psim.catalog.v1.Device](#psim-catalog-v1-Device) |  |  |






<a name="psim-api-v1-CreateDeviceTypeRequest"></a>

### CreateDeviceTypeRequest
CreateDeviceTypeRequest is the request of CreateDeviceType.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| device_type | [psim.catalog.v1.DeviceType](#psim-catalog-v1-DeviceType) |  |  |






<a name="psim-api-v1-CreateDeviceTypeResponse"></a>

### CreateDeviceTypeResponse
CreateDeviceTypeResponse is the response of CreateDeviceType.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| device_type | [psim.catalog.v1.DeviceType](#psim-catalog-v1-DeviceType) |  |  |






<a name="psim-api-v1-CreateLocationRequest"></a>

### CreateLocationRequest
CreateLocationRequest is the request of CreateLocation.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| location | [psim.catalog.v1.Location](#psim-catalog-v1-Location) |  |  |






<a name="psim-api-v1-CreateLocationResponse"></a>

### CreateLocationResponse
CreateLocationResponse is the response of CreateLocation.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| location | [psim.catalog.v1.Location](#psim-catalog-v1-Location) |  |  |






<a name="psim-api-v1-ExportDevicesRequest"></a>

### ExportDevicesRequest
ExportDevicesRequest is the request of ExportDevices.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| location_id | [string](#string) |  |  |
| format | [string](#string) |  |  |






<a name="psim-api-v1-ExportDevicesResponse"></a>

### ExportDevicesResponse
ExportDevicesResponse is the response of ExportDevices; the body is the file.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| file | [google.api.HttpBody](#google-api-HttpBody) |  |  |






<a name="psim-api-v1-GetDeviceRequest"></a>

### GetDeviceRequest
GetDeviceRequest is the request of GetDevice.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| device_id | [string](#string) |  |  |






<a name="psim-api-v1-GetDeviceResponse"></a>

### GetDeviceResponse
GetDeviceResponse is the response of GetDevice.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| device | [psim.catalog.v1.Device](#psim-catalog-v1-Device) |  |  |






<a name="psim-api-v1-GetFloorPlanRequest"></a>

### GetFloorPlanRequest
GetFloorPlanRequest is the request of GetFloorPlan.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| location_id | [string](#string) |  |  |






<a name="psim-api-v1-GetFloorPlanResponse"></a>

### GetFloorPlanResponse
GetFloorPlanResponse is the response of GetFloorPlan; the body is the image.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| image | [google.api.HttpBody](#google-api-HttpBody) |  |  |






<a name="psim-api-v1-GetImportRequest"></a>

### GetImportRequest
GetImportRequest is the request of GetImport.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| import_id | [string](#string) |  |  |






<a name="psim-api-v1-GetImportResponse"></a>

### GetImportResponse
GetImportResponse is the response of GetImport.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| import_job | [ImportJob](#psim-api-v1-ImportJob) |  |  |






<a name="psim-api-v1-GetLocationRequest"></a>

### GetLocationRequest
GetLocationRequest is the request of GetLocation.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| location_id | [string](#string) |  |  |






<a name="psim-api-v1-GetLocationResponse"></a>

### GetLocationResponse
GetLocationResponse is the response of GetLocation.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| location | [psim.catalog.v1.Location](#psim-catalog-v1-Location) |  |  |






<a name="psim-api-v1-ImportJob"></a>

### ImportJob
ImportJob is a bulk import and its results (aggregates.md, 1.5).


| Field | Type | Label | Description |
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
ImportRowError is an error of one imported row.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| row | [uint32](#uint32) |  |  |
| code | [string](#string) |  |  |
| message | [string](#string) |  |  |






<a name="psim-api-v1-ListConnectorsRequest"></a>

### ListConnectorsRequest
ListConnectorsRequest is the request of ListConnectors.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListConnectorsResponse"></a>

### ListConnectorsResponse
ListConnectorsResponse is the response of ListConnectors.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| connectors | [ConnectorStatus](#psim-api-v1-ConnectorStatus) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListDeviceTypesRequest"></a>

### ListDeviceTypesRequest
ListDeviceTypesRequest is the request of ListDeviceTypes.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListDeviceTypesResponse"></a>

### ListDeviceTypesResponse
ListDeviceTypesResponse is the response of ListDeviceTypes.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| device_types | [psim.catalog.v1.DeviceType](#psim-catalog-v1-DeviceType) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListDevicesRequest"></a>

### ListDevicesRequest
ListDevicesRequest is the request of ListDevices.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| location_id | [string](#string) |  | Any location; devices of all nested zones are returned. |
| connector_id | [string](#string) |  |  |
| device_type_id | [string](#string) |  |  |
| state | [psim.catalog.v1.ResourceState](#psim-catalog-v1-ResourceState) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListDevicesResponse"></a>

### ListDevicesResponse
ListDevicesResponse is the response of ListDevices.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| devices | [psim.catalog.v1.Device](#psim-catalog-v1-Device) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListLocationsRequest"></a>

### ListLocationsRequest
ListLocationsRequest is the request of ListLocations.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| parent_id | [string](#string) |  |  |
| kind | [psim.catalog.v1.LocationKind](#psim-catalog-v1-LocationKind) |  |  |
| include_archived | [bool](#bool) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListLocationsResponse"></a>

### ListLocationsResponse
ListLocationsResponse is the response of ListLocations.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| locations | [psim.catalog.v1.Location](#psim-catalog-v1-Location) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-RegisterConnectorRequest"></a>

### RegisterConnectorRequest
RegisterConnectorRequest is the request of RegisterConnector.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| connector | [psim.catalog.v1.Connector](#psim-catalog-v1-Connector) |  |  |






<a name="psim-api-v1-RegisterConnectorResponse"></a>

### RegisterConnectorResponse
RegisterConnectorResponse is the response of RegisterConnector.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| connector | [psim.catalog.v1.Connector](#psim-catalog-v1-Connector) |  |  |






<a name="psim-api-v1-RevokeConnectorRequest"></a>

### RevokeConnectorRequest
RevokeConnectorRequest is the request of RevokeConnector.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| connector_id | [string](#string) |  |  |
| reason | [string](#string) |  |  |






<a name="psim-api-v1-RevokeConnectorResponse"></a>

### RevokeConnectorResponse
RevokeConnectorResponse is the response of RevokeConnector.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| connector | [psim.catalog.v1.Connector](#psim-catalog-v1-Connector) |  |  |






<a name="psim-api-v1-RotateConnectorCertificateRequest"></a>

### RotateConnectorCertificateRequest
RotateConnectorCertificateRequest is the request of RotateConnectorCertificate.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| connector_id | [string](#string) |  |  |
| new_certificate | [psim.catalog.v1.CertificateFingerprint](#psim-catalog-v1-CertificateFingerprint) |  |  |
| previous_not_after | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  | Expiry of the previous certificate. |






<a name="psim-api-v1-RotateConnectorCertificateResponse"></a>

### RotateConnectorCertificateResponse
RotateConnectorCertificateResponse is the response of RotateConnectorCertificate.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| connector | [psim.catalog.v1.Connector](#psim-catalog-v1-Connector) |  |  |






<a name="psim-api-v1-StartImportRequest"></a>

### StartImportRequest
StartImportRequest is the request of StartImport; the body is the file.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| format | [string](#string) |  | &#34;csv&#34; or &#34;json&#34;; passed as a query parameter. |
| dry_run | [bool](#bool) |  | Validate only, do not apply. |
| file | [google.api.HttpBody](#google-api-HttpBody) |  |  |






<a name="psim-api-v1-StartImportResponse"></a>

### StartImportResponse
StartImportResponse is the response of StartImport.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| import_job | [ImportJob](#psim-api-v1-ImportJob) |  |  |






<a name="psim-api-v1-UpdateDeviceRequest"></a>

### UpdateDeviceRequest
UpdateDeviceRequest is the request of UpdateDevice.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| device_id | [string](#string) |  |  |
| device | [psim.catalog.v1.Device](#psim-catalog-v1-Device) |  |  |






<a name="psim-api-v1-UpdateDeviceResponse"></a>

### UpdateDeviceResponse
UpdateDeviceResponse is the response of UpdateDevice.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| device | [psim.catalog.v1.Device](#psim-catalog-v1-Device) |  |  |






<a name="psim-api-v1-UpdateDeviceTypeRequest"></a>

### UpdateDeviceTypeRequest
UpdateDeviceTypeRequest is the request of UpdateDeviceType.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| device_type_id | [string](#string) |  |  |
| device_type | [psim.catalog.v1.DeviceType](#psim-catalog-v1-DeviceType) |  |  |






<a name="psim-api-v1-UpdateDeviceTypeResponse"></a>

### UpdateDeviceTypeResponse
UpdateDeviceTypeResponse is the response of UpdateDeviceType.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| device_type | [psim.catalog.v1.DeviceType](#psim-catalog-v1-DeviceType) |  |  |






<a name="psim-api-v1-UpdateLocationRequest"></a>

### UpdateLocationRequest
UpdateLocationRequest is the request of UpdateLocation.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| location_id | [string](#string) |  |  |
| location | [psim.catalog.v1.Location](#psim-catalog-v1-Location) |  |  |






<a name="psim-api-v1-UpdateLocationResponse"></a>

### UpdateLocationResponse
UpdateLocationResponse is the response of UpdateLocation.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| location | [psim.catalog.v1.Location](#psim-catalog-v1-Location) |  |  |






<a name="psim-api-v1-UploadFloorPlanRequest"></a>

### UploadFloorPlanRequest
UploadFloorPlanRequest is the request of UploadFloorPlan; the body is the image.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| location_id | [string](#string) |  |  |
| width | [double](#double) |  | Plan size in plan units; passed as query parameters. |
| height | [double](#double) |  |  |
| image | [google.api.HttpBody](#google-api-HttpBody) |  |  |






<a name="psim-api-v1-UploadFloorPlanResponse"></a>

### UploadFloorPlanResponse
UploadFloorPlanResponse is the response of UploadFloorPlan.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| location | [psim.catalog.v1.Location](#psim-catalog-v1-Location) |  |  |





 


<a name="psim-api-v1-ImportState"></a>

### ImportState
ImportState is the state of an import job.

| Name | Number | Description |
| ---- | ------ | ----------- |
| IMPORT_STATE_UNSPECIFIED | 0 |  |
| IMPORT_STATE_RUNNING | 1 |  |
| IMPORT_STATE_COMPLETED | 2 |  |
| IMPORT_STATE_FAILED | 3 |  |


 

 


<a name="psim-api-v1-CatalogService"></a>

### CatalogService
CatalogService manages sites, buildings, floors, zones, device types, devices and connectors.
Owner: Resource Catalog. Scenarios S1, S2.

| Method Name | Request Type | Response Type | Description |
| ----------- | ------------ | ------------- | ------------|
| ListLocations | [ListLocationsRequest](#psim-api-v1-ListLocationsRequest) | [ListLocationsResponse](#psim-api-v1-ListLocationsResponse) | ListLocations lists locations, filtered by parent or kind. |
| GetLocation | [GetLocationRequest](#psim-api-v1-GetLocationRequest) | [GetLocationResponse](#psim-api-v1-GetLocationResponse) | GetLocation returns one location. |
| CreateLocation | [CreateLocationRequest](#psim-api-v1-CreateLocationRequest) | [CreateLocationResponse](#psim-api-v1-CreateLocationResponse) | CreateLocation creates a site, building, floor or zone. |
| UpdateLocation | [UpdateLocationRequest](#psim-api-v1-UpdateLocationRequest) | [UpdateLocationResponse](#psim-api-v1-UpdateLocationResponse) | UpdateLocation replaces mutable fields of a location (If-Match required). |
| ArchiveLocation | [ArchiveLocationRequest](#psim-api-v1-ArchiveLocationRequest) | [ArchiveLocationResponse](#psim-api-v1-ArchiveLocationResponse) | ArchiveLocation archives a location without children and devices (L3). |
| UploadFloorPlan | [UploadFloorPlanRequest](#psim-api-v1-UploadFloorPlanRequest) | [UploadFloorPlanResponse](#psim-api-v1-UploadFloorPlanResponse) | UploadFloorPlan uploads the floor plan image of a floor. |
| GetFloorPlan | [GetFloorPlanRequest](#psim-api-v1-GetFloorPlanRequest) | [GetFloorPlanResponse](#psim-api-v1-GetFloorPlanResponse) | GetFloorPlan downloads the floor plan image. |
| ListDeviceTypes | [ListDeviceTypesRequest](#psim-api-v1-ListDeviceTypesRequest) | [ListDeviceTypesResponse](#psim-api-v1-ListDeviceTypesResponse) | ListDeviceTypes lists device types. |
| CreateDeviceType | [CreateDeviceTypeRequest](#psim-api-v1-CreateDeviceTypeRequest) | [CreateDeviceTypeResponse](#psim-api-v1-CreateDeviceTypeResponse) | CreateDeviceType creates a device type. |
| UpdateDeviceType | [UpdateDeviceTypeRequest](#psim-api-v1-UpdateDeviceTypeRequest) | [UpdateDeviceTypeResponse](#psim-api-v1-UpdateDeviceTypeResponse) | UpdateDeviceType replaces a device type (If-Match required). |
| ListDevices | [ListDevicesRequest](#psim-api-v1-ListDevicesRequest) | [ListDevicesResponse](#psim-api-v1-ListDevicesResponse) | ListDevices lists devices, filtered by location, connector or state. |
| GetDevice | [GetDeviceRequest](#psim-api-v1-GetDeviceRequest) | [GetDeviceResponse](#psim-api-v1-GetDeviceResponse) | GetDevice returns one device. |
| CreateDevice | [CreateDeviceRequest](#psim-api-v1-CreateDeviceRequest) | [CreateDeviceResponse](#psim-api-v1-CreateDeviceResponse) | CreateDevice registers a device with its sources. |
| UpdateDevice | [UpdateDeviceRequest](#psim-api-v1-UpdateDeviceRequest) | [UpdateDeviceResponse](#psim-api-v1-UpdateDeviceResponse) | UpdateDevice replaces a device, including zone, position and state (If-Match required). |
| StartImport | [StartImportRequest](#psim-api-v1-StartImportRequest) | [StartImportResponse](#psim-api-v1-StartImportResponse) | StartImport starts a bulk import of devices from CSV or JSON. |
| GetImport | [GetImportRequest](#psim-api-v1-GetImportRequest) | [GetImportResponse](#psim-api-v1-GetImportResponse) | GetImport returns import progress and per-row results. |
| ExportDevices | [ExportDevicesRequest](#psim-api-v1-ExportDevicesRequest) | [ExportDevicesResponse](#psim-api-v1-ExportDevicesResponse) | ExportDevices exports devices in CSV or JSON. |
| ListConnectors | [ListConnectorsRequest](#psim-api-v1-ListConnectorsRequest) | [ListConnectorsResponse](#psim-api-v1-ListConnectorsResponse) | ListConnectors lists registered connectors with their status. |
| RegisterConnector | [RegisterConnectorRequest](#psim-api-v1-RegisterConnectorRequest) | [RegisterConnectorResponse](#psim-api-v1-RegisterConnectorResponse) | RegisterConnector registers a connector and binds its certificate. |
| RotateConnectorCertificate | [RotateConnectorCertificateRequest](#psim-api-v1-RotateConnectorCertificateRequest) | [RotateConnectorCertificateResponse](#psim-api-v1-RotateConnectorCertificateResponse) | RotateConnectorCertificate adds a new certificate and sets expiry of the old one (C1). |
| RevokeConnector | [RevokeConnectorRequest](#psim-api-v1-RevokeConnectorRequest) | [RevokeConnectorResponse](#psim-api-v1-RevokeConnectorResponse) | RevokeConnector revokes a connector and closes its sessions (C2). |

 



<a name="psim_response_v1_command-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/response/v1/command.proto



<a name="psim-response-v1-Command"></a>

### Command
Command is a request to act on a device (aggregates.md, 5.3; state-models.md, 3).


| Field | Type | Label | Description |
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
| result_code | [string](#string) |  | Error code from the catalog for failed, rejected, timed out or cancelled commands. |
| result_message | [string](#string) |  |  |
| version | [uint64](#uint64) |  |  |






<a name="psim-response-v1-Command-ParametersEntry"></a>

### Command.ParametersEntry



| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-response-v1-CommandDelivery"></a>

### CommandDelivery
CommandDelivery is a command routed to the connector session.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |
| connector_id | [string](#string) |  |  |
| device_external_id | [string](#string) |  |  |
| capability | [string](#string) |  |  |
| parameters | [CommandDelivery.ParametersEntry](#psim-response-v1-CommandDelivery-ParametersEntry) | repeated |  |
| deadline_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-response-v1-CommandDelivery-ParametersEntry"></a>

### CommandDelivery.ParametersEntry



| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-response-v1-CommandDeliveryRecord"></a>

### CommandDeliveryRecord
CommandDeliveryRecord is the value of topic psim.commands.v1 (key: connector_id).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| command_delivery | [CommandDelivery](#psim-response-v1-CommandDelivery) |  |  |






<a name="psim-response-v1-CommandOutcome"></a>

### CommandOutcome
CommandOutcome reports the final result from the connector or the gateway.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |
| success | [bool](#bool) |  |  |
| code | [string](#string) |  |  |
| message | [string](#string) |  |  |






<a name="psim-response-v1-CommandReceipt"></a>

### CommandReceipt
CommandReceipt reports that the connector received a command.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |






<a name="psim-response-v1-CommandResultRecord"></a>

### CommandResultRecord
CommandResultRecord is the value of topic psim.commands.results.v1 (key: command_id).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| command_receipt | [CommandReceipt](#psim-response-v1-CommandReceipt) |  |  |
| command_outcome | [CommandOutcome](#psim-response-v1-CommandOutcome) |  |  |





 


<a name="psim-response-v1-CommandState"></a>

### CommandState
CommandState is the lifecycle state of a command.

| Name | Number | Description |
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


 

 

 



<a name="psim_api_v1_commands-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/api/v1/commands.proto



<a name="psim-api-v1-ApproveCommandRequest"></a>

### ApproveCommandRequest
ApproveCommandRequest is the request of ApproveCommand.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |






<a name="psim-api-v1-ApproveCommandResponse"></a>

### ApproveCommandResponse
ApproveCommandResponse is the response of ApproveCommand.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| command | [psim.response.v1.Command](#psim-response-v1-Command) |  |  |






<a name="psim-api-v1-CancelCommandRequest"></a>

### CancelCommandRequest
CancelCommandRequest is the request of CancelCommand.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |






<a name="psim-api-v1-CancelCommandResponse"></a>

### CancelCommandResponse
CancelCommandResponse is the response of CancelCommand.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| command | [psim.response.v1.Command](#psim-response-v1-Command) |  |  |






<a name="psim-api-v1-GetCommandRequest"></a>

### GetCommandRequest
GetCommandRequest is the request of GetCommand.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |






<a name="psim-api-v1-GetCommandResponse"></a>

### GetCommandResponse
GetCommandResponse is the response of GetCommand.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| command | [psim.response.v1.Command](#psim-response-v1-Command) |  |  |






<a name="psim-api-v1-ListCommandsRequest"></a>

### ListCommandsRequest
ListCommandsRequest is the request of ListCommands.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| device_id | [string](#string) |  |  |
| states | [psim.response.v1.CommandState](#psim-response-v1-CommandState) | repeated |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListCommandsResponse"></a>

### ListCommandsResponse
ListCommandsResponse is the response of ListCommands.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| commands | [psim.response.v1.Command](#psim-response-v1-Command) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-RejectCommandRequest"></a>

### RejectCommandRequest
RejectCommandRequest is the request of RejectCommand.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |
| reason | [string](#string) |  |  |






<a name="psim-api-v1-RejectCommandResponse"></a>

### RejectCommandResponse
RejectCommandResponse is the response of RejectCommand.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| command | [psim.response.v1.Command](#psim-response-v1-Command) |  |  |






<a name="psim-api-v1-RequestCommandRequest"></a>

### RequestCommandRequest
RequestCommandRequest is the request of RequestCommand.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |
| device_id | [string](#string) |  |  |
| capability | [string](#string) |  |  |
| parameters | [RequestCommandRequest.ParametersEntry](#psim-api-v1-RequestCommandRequest-ParametersEntry) | repeated |  |
| incident_id | [string](#string) |  |  |
| run_id | [string](#string) |  |  |
| step_id | [string](#string) |  |  |






<a name="psim-api-v1-RequestCommandRequest-ParametersEntry"></a>

### RequestCommandRequest.ParametersEntry



| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-api-v1-RequestCommandResponse"></a>

### RequestCommandResponse
RequestCommandResponse is the response of RequestCommand.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| command | [psim.response.v1.Command](#psim-response-v1-Command) |  |  |





 

 

 


<a name="psim-api-v1-CommandService"></a>

### CommandService
CommandService manages commands to devices. Owner: Command Service. Scenario S8.

| Method Name | Request Type | Response Type | Description |
| ----------- | ------------ | ------------- | ------------|
| RequestCommand | [RequestCommandRequest](#psim-api-v1-RequestCommandRequest) | [RequestCommandResponse](#psim-api-v1-RequestCommandResponse) | RequestCommand requests a command; Idempotency-Key header equals command_id. |
| GetCommand | [GetCommandRequest](#psim-api-v1-GetCommandRequest) | [GetCommandResponse](#psim-api-v1-GetCommandResponse) | GetCommand returns one command. |
| ListCommands | [ListCommandsRequest](#psim-api-v1-ListCommandsRequest) | [ListCommandsResponse](#psim-api-v1-ListCommandsResponse) | ListCommands lists commands by incident, device or state. |
| ApproveCommand | [ApproveCommandRequest](#psim-api-v1-ApproveCommandRequest) | [ApproveCommandResponse](#psim-api-v1-ApproveCommandResponse) | ApproveCommand approves a critical command; approver must differ from requester (CM3). |
| RejectCommand | [RejectCommandRequest](#psim-api-v1-RejectCommandRequest) | [RejectCommandResponse](#psim-api-v1-RejectCommandResponse) | RejectCommand rejects a critical command. |
| CancelCommand | [CancelCommandRequest](#psim-api-v1-CancelCommandRequest) | [CancelCommandResponse](#psim-api-v1-CancelCommandResponse) | CancelCommand cancels a command before it is sent. |

 



<a name="psim_processing_v1_event-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/processing/v1/event.proto



<a name="psim-processing-v1-Event"></a>

### Event
Event is a normalized event. envelope.message_id equals the raw event_id,
envelope.sequence equals the source sequence number.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| type | [string](#string) |  | Taxonomy type code, for example &#34;intrusion.zone.alarm&#34;. |
| taxonomy_version | [string](#string) |  | Taxonomy version used for normalization, for example &#34;1&#34;. |
| event_class | [psim.common.v1.EventClass](#psim-common-v1-EventClass) |  |  |
| severity | [psim.common.v1.Severity](#psim-common-v1-Severity) |  |  |
| site_id | [string](#string) |  |  |
| zone_id | [string](#string) |  |  |
| device_id | [string](#string) |  |  |
| source_id | [string](#string) |  |  |
| connector_id | [string](#string) |  |  |
| source_epoch | [uint32](#uint32) |  |  |
| raw_code | [string](#string) |  | Vendor event code, always preserved. |
| attributes | [Event.AttributesEntry](#psim-processing-v1-Event-AttributesEntry) | repeated | Standard attributes (event-taxonomy.md, section 6) and connector attributes prefixed with &#34;x.&#34;. |
| significant | [bool](#bool) |  | Significant events are stored long-term in Event History (ADR-028). |
| clock_skew_corrected | [bool](#bool) |  | Event time was corrected because of source clock skew (ADR-010). |






<a name="psim-processing-v1-Event-AttributesEntry"></a>

### Event.AttributesEntry



| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-processing-v1-EventRecord"></a>

### EventRecord
EventRecord is the value of topic psim.events.normalized.v1 (key: site_id:zone_id).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| event | [Event](#psim-processing-v1-Event) |  |  |





 

 

 

 



<a name="psim_api_v1_events-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/api/v1/events.proto



<a name="psim-api-v1-GetEventRequest"></a>

### GetEventRequest
GetEventRequest is the request of GetEvent.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| event_id | [string](#string) |  |  |






<a name="psim-api-v1-GetEventResponse"></a>

### GetEventResponse
GetEventResponse is the response of GetEvent.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| event | [StoredEvent](#psim-api-v1-StoredEvent) |  |  |






<a name="psim-api-v1-ListIncidentEventsRequest"></a>

### ListIncidentEventsRequest
ListIncidentEventsRequest is the request of ListIncidentEvents.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListIncidentEventsResponse"></a>

### ListIncidentEventsResponse
ListIncidentEventsResponse is the response of ListIncidentEvents.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| events | [StoredEvent](#psim-api-v1-StoredEvent) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-SearchEventsRequest"></a>

### SearchEventsRequest
SearchEventsRequest is the request of SearchEvents.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| from | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| to | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| site_ids | [string](#string) | repeated |  |
| zone_ids | [string](#string) | repeated |  |
| device_ids | [string](#string) | repeated |  |
| types | [string](#string) | repeated | Taxonomy codes or prefixes ending with &#34;.*&#34;. |
| min_severity | [psim.common.v1.Severity](#psim-common-v1-Severity) |  |  |
| significant_only | [bool](#bool) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-SearchEventsResponse"></a>

### SearchEventsResponse
SearchEventsResponse is the response of SearchEvents.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| events | [StoredEvent](#psim-api-v1-StoredEvent) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-StoredEvent"></a>

### StoredEvent
StoredEvent is a normalized event with its envelope times.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| event_id | [string](#string) |  |  |
| occurred_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| received_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| source_seq | [uint64](#uint64) |  |  |
| event | [psim.processing.v1.Event](#psim-processing-v1-Event) |  |  |





 

 

 


<a name="psim-api-v1-EventHistoryService"></a>

### EventHistoryService
EventHistoryService searches stored events (ADR-028). Owner: Event History. Scenario S7.

| Method Name | Request Type | Response Type | Description |
| ----------- | ------------ | ------------- | ------------|
| SearchEvents | [SearchEventsRequest](#psim-api-v1-SearchEventsRequest) | [SearchEventsResponse](#psim-api-v1-SearchEventsResponse) | SearchEvents searches events by location, device, type and time; newest first. |
| GetEvent | [GetEventRequest](#psim-api-v1-GetEventRequest) | [GetEventResponse](#psim-api-v1-GetEventResponse) | GetEvent returns one event. |
| ListIncidentEvents | [ListIncidentEventsRequest](#psim-api-v1-ListIncidentEventsRequest) | [ListIncidentEventsResponse](#psim-api-v1-ListIncidentEventsResponse) | ListIncidentEvents returns source events of all signals of an incident. |

 



<a name="psim_incident_v1_incident-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/incident/v1/incident.proto



<a name="psim-incident-v1-Incident"></a>

### Incident
Incident is the central aggregate (aggregates.md, 4.2; state-models.md, 1; ADR-027).


| Field | Type | Label | Description |
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
| signal_ids | [string](#string) | repeated |  |
| title | [string](#string) |  |  |
| assignee_id | [string](#string) |  | User id of the responsible operator. |
| escalation_level | [uint32](#uint32) |  | 0 means not escalated. |
| blocking_steps | [StepRef](#psim-incident-v1-StepRef) | repeated | Unfinished mandatory steps of the active response run (I7). |
| response_run_version | [uint64](#uint64) |  | Version of the response run applied to blocking_steps. |
| resolution | [Resolution](#psim-incident-v1-Resolution) |  |  |
| resolution_note | [string](#string) |  |  |
| related_incident_id | [string](#string) |  | Previous incident with the same grouping key, if any (I4). |
| created_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| acknowledged_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| resolved_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| closed_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| version | [uint64](#uint64) |  | Aggregate version, used as ETag. |






<a name="psim-incident-v1-IncidentType"></a>

### IncidentType
IncidentType is a class of situations that defines response plan and SLA (aggregates.md, 4.1).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_type_id | [string](#string) |  |  |
| code | [string](#string) |  |  |
| name | [string](#string) |  |  |
| default_priority | [psim.common.v1.Priority](#psim-common-v1-Priority) |  |  |
| grouping_window | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| escalation_policy_id | [string](#string) |  |  |
| response_required | [bool](#bool) |  | When set, an incident without a matching response plan is flagged. |
| auto_close_after | [google.protobuf.Duration](#google-protobuf-Duration) |  | Delay between resolved and automatic close. |
| version | [uint64](#uint64) |  |  |






<a name="psim-incident-v1-Link"></a>

### Link
Link is an external reference attached to an incident (video clip, document).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| link_id | [string](#string) |  |  |
| url | [string](#string) |  |  |
| title | [string](#string) |  |  |






<a name="psim-incident-v1-StepRef"></a>

### StepRef
StepRef points to a step of a response run.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| run_id | [string](#string) |  |  |
| step_id | [string](#string) |  |  |
| title | [string](#string) |  |  |






<a name="psim-incident-v1-TimelineEntry"></a>

### TimelineEntry
TimelineEntry is one record of an incident timeline.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| entry_id | [string](#string) |  |  |
| incident_id | [string](#string) |  |  |
| at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| actor | [psim.common.v1.Actor](#psim-common-v1-Actor) |  |  |
| kind | [TimelineEntryKind](#psim-incident-v1-TimelineEntryKind) |  |  |
| text | [string](#string) |  |  |
| ref_id | [string](#string) |  | Id of the related object: signal, step, command, notification, link. |
| details | [TimelineEntry.DetailsEntry](#psim-incident-v1-TimelineEntry-DetailsEntry) | repeated |  |






<a name="psim-incident-v1-TimelineEntry-DetailsEntry"></a>

### TimelineEntry.DetailsEntry



| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |





 


<a name="psim-incident-v1-IncidentState"></a>

### IncidentState
IncidentState is the lifecycle state of an incident.

| Name | Number | Description |
| ---- | ------ | ----------- |
| INCIDENT_STATE_UNSPECIFIED | 0 |  |
| INCIDENT_STATE_NEW | 1 |  |
| INCIDENT_STATE_ACKNOWLEDGED | 2 |  |
| INCIDENT_STATE_IN_PROGRESS | 3 |  |
| INCIDENT_STATE_RESOLVED | 4 |  |
| INCIDENT_STATE_CLOSED | 5 |  |



<a name="psim-incident-v1-PrioritySource"></a>

### PrioritySource
PrioritySource tells whether the priority was computed or set manually (I6).

| Name | Number | Description |
| ---- | ------ | ----------- |
| PRIORITY_SOURCE_UNSPECIFIED | 0 |  |
| PRIORITY_SOURCE_COMPUTED | 1 |  |
| PRIORITY_SOURCE_MANUAL | 2 |  |



<a name="psim-incident-v1-Resolution"></a>

### Resolution
Resolution is the outcome of an incident.

| Name | Number | Description |
| ---- | ------ | ----------- |
| RESOLUTION_UNSPECIFIED | 0 |  |
| RESOLUTION_CONFIRMED | 1 |  |
| RESOLUTION_FALSE_ALARM | 2 |  |
| RESOLUTION_TEST | 3 |  |
| RESOLUTION_DUPLICATE | 4 |  |



<a name="psim-incident-v1-TimelineEntryKind"></a>

### TimelineEntryKind
TimelineEntryKind classifies timeline entries.

| Name | Number | Description |
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
<p align="right"><a href="#top">Top</a></p>

## psim/api/v1/feed.proto



<a name="psim-api-v1-DeviceStatus"></a>

### DeviceStatus
DeviceStatus is the current state of a device derived from alarm, fault, restore and state events.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| device_id | [string](#string) |  |  |
| state | [psim.common.v1.EventClass](#psim-common-v1-EventClass) |  |  |
| last_type | [string](#string) |  | Last state-changing taxonomy type. |
| last_changed_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| online | [bool](#bool) |  |  |






<a name="psim-api-v1-GetIncidentFeedRequest"></a>

### GetIncidentFeedRequest
GetIncidentFeedRequest is the request of GetIncidentFeed.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| site_ids | [string](#string) | repeated |  |
| zone_ids | [string](#string) | repeated |  |






<a name="psim-api-v1-GetIncidentFeedResponse"></a>

### GetIncidentFeedResponse
GetIncidentFeedResponse is the response of GetIncidentFeed.
Not paginated: the console needs the whole feed (up to 10 000 incidents).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incidents | [psim.incident.v1.Incident](#psim-incident-v1-Incident) | repeated |  |
| resume_token | [string](#string) |  | Token for the realtime Subscribe frame. |
| snapshot_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-api-v1-GetLocationStatusRequest"></a>

### GetLocationStatusRequest
GetLocationStatusRequest is the request of GetLocationStatus.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| location_id | [string](#string) |  |  |






<a name="psim-api-v1-GetLocationStatusResponse"></a>

### GetLocationStatusResponse
GetLocationStatusResponse is the response of GetLocationStatus.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| zones | [ZoneStatus](#psim-api-v1-ZoneStatus) | repeated |  |
| devices | [DeviceStatus](#psim-api-v1-DeviceStatus) | repeated |  |






<a name="psim-api-v1-GetOperatorLoadRequest"></a>

### GetOperatorLoadRequest
GetOperatorLoadRequest is the request of GetOperatorLoad.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| site_ids | [string](#string) | repeated |  |






<a name="psim-api-v1-GetOperatorLoadResponse"></a>

### GetOperatorLoadResponse
GetOperatorLoadResponse is the response of GetOperatorLoad.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| operators | [OperatorLoad](#psim-api-v1-OperatorLoad) | repeated |  |






<a name="psim-api-v1-OperatorLoad"></a>

### OperatorLoad
OperatorLoad is the current load of one operator.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| user_id | [string](#string) |  |  |
| display_name | [string](#string) |  |  |
| assigned_open | [uint32](#uint32) |  |  |
| escalated | [uint32](#uint32) |  |  |






<a name="psim-api-v1-ZoneStatus"></a>

### ZoneStatus
ZoneStatus is the aggregated state of a zone.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| zone_id | [string](#string) |  |  |
| state | [psim.common.v1.EventClass](#psim-common-v1-EventClass) |  | Highest class among active device states. |
| armed | [bool](#bool) |  |  |
| open_incidents | [uint32](#uint32) |  |  |





 

 

 


<a name="psim-api-v1-FeedService"></a>

### FeedService
FeedService serves console read models. Owner: Projection Service. Scenarios S6, S11.

| Method Name | Request Type | Response Type | Description |
| ----------- | ------------ | ------------- | ------------|
| GetIncidentFeed | [GetIncidentFeedRequest](#psim-api-v1-GetIncidentFeedRequest) | [GetIncidentFeedResponse](#psim-api-v1-GetIncidentFeedResponse) | GetIncidentFeed returns active incidents of the caller scope and the resume token at which the snapshot is consistent (ADR-019). |
| GetLocationStatus | [GetLocationStatusRequest](#psim-api-v1-GetLocationStatusRequest) | [GetLocationStatusResponse](#psim-api-v1-GetLocationStatusResponse) | GetLocationStatus returns the state of devices and zones of a location for the object view. |
| GetOperatorLoad | [GetOperatorLoadRequest](#psim-api-v1-GetOperatorLoadRequest) | [GetOperatorLoadResponse](#psim-api-v1-GetOperatorLoadResponse) | GetOperatorLoad returns the current load of operators (supervisor). |

 



<a name="psim_processing_v1_signal-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/processing/v1/signal.proto



<a name="psim-processing-v1-Signal"></a>

### Signal
Signal is the result of a correlation rule firing or of an external producer (ADR-025).
envelope.message_id is the deterministic signal_id (ADR-005).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| origin | [SignalOrigin](#psim-processing-v1-SignalOrigin) |  |  |
| correlation_key | [string](#string) |  |  |
| grouping_key | [string](#string) |  | Key used by Incident Service to attach the signal to an open incident. |
| incident_type_id | [string](#string) |  |  |
| priority | [psim.common.v1.Priority](#psim-common-v1-Priority) |  |  |
| site_id | [string](#string) |  |  |
| zone_id | [string](#string) |  | Empty for signals of rules wider than a zone. |
| event_ids | [string](#string) | repeated |  |
| first_event_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| last_event_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| summary | [string](#string) |  |  |
| explanation_ref | [string](#string) |  | Optional reference to an explanation produced by an external module. |






<a name="psim-processing-v1-SignalOrigin"></a>

### SignalOrigin
SignalOrigin identifies what produced a signal.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| kind | [SignalOriginKind](#psim-processing-v1-SignalOriginKind) |  |  |
| rule_id | [string](#string) |  | Correlation rule id for SIGNAL_ORIGIN_KIND_RULE. |
| rule_version | [uint32](#uint32) |  |  |
| producer | [string](#string) |  | Registered external producer name for SIGNAL_ORIGIN_KIND_EXTERNAL. |






<a name="psim-processing-v1-SignalRecord"></a>

### SignalRecord
SignalRecord is the value of topic psim.signals.v1 (key: grouping_key).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| signal | [Signal](#psim-processing-v1-Signal) |  |  |





 


<a name="psim-processing-v1-SignalOriginKind"></a>

### SignalOriginKind
SignalOriginKind is the kind of signal producer.

| Name | Number | Description |
| ---- | ------ | ----------- |
| SIGNAL_ORIGIN_KIND_UNSPECIFIED | 0 |  |
| SIGNAL_ORIGIN_KIND_RULE | 1 |  |
| SIGNAL_ORIGIN_KIND_EXTERNAL | 2 |  |


 

 

 



<a name="psim_api_v1_incidents-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/api/v1/incidents.proto



<a name="psim-api-v1-AcknowledgeIncidentRequest"></a>

### AcknowledgeIncidentRequest
AcknowledgeIncidentRequest is the request of AcknowledgeIncident.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |






<a name="psim-api-v1-AcknowledgeIncidentResponse"></a>

### AcknowledgeIncidentResponse
AcknowledgeIncidentResponse is the response of AcknowledgeIncident.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-api-v1-AddIncidentCommentRequest"></a>

### AddIncidentCommentRequest
AddIncidentCommentRequest is the request of AddIncidentComment.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| text | [string](#string) |  |  |






<a name="psim-api-v1-AddIncidentCommentResponse"></a>

### AddIncidentCommentResponse
AddIncidentCommentResponse is the response of AddIncidentComment.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| entry | [psim.incident.v1.TimelineEntry](#psim-incident-v1-TimelineEntry) |  |  |






<a name="psim-api-v1-AddIncidentLinkRequest"></a>

### AddIncidentLinkRequest
AddIncidentLinkRequest is the request of AddIncidentLink.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| url | [string](#string) |  |  |
| title | [string](#string) |  |  |






<a name="psim-api-v1-AddIncidentLinkResponse"></a>

### AddIncidentLinkResponse
AddIncidentLinkResponse is the response of AddIncidentLink.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| link | [psim.incident.v1.Link](#psim-incident-v1-Link) |  |  |






<a name="psim-api-v1-AssignIncidentRequest"></a>

### AssignIncidentRequest
AssignIncidentRequest is the request of AssignIncident.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| assignee_id | [string](#string) |  |  |
| reason | [string](#string) |  |  |






<a name="psim-api-v1-AssignIncidentResponse"></a>

### AssignIncidentResponse
AssignIncidentResponse is the response of AssignIncident.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-api-v1-CloseIncidentRequest"></a>

### CloseIncidentRequest
CloseIncidentRequest is the request of CloseIncident.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |






<a name="psim-api-v1-CloseIncidentResponse"></a>

### CloseIncidentResponse
CloseIncidentResponse is the response of CloseIncident.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-api-v1-CreateIncidentTypeRequest"></a>

### CreateIncidentTypeRequest
CreateIncidentTypeRequest is the request of CreateIncidentType.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_type | [psim.incident.v1.IncidentType](#psim-incident-v1-IncidentType) |  |  |






<a name="psim-api-v1-CreateIncidentTypeResponse"></a>

### CreateIncidentTypeResponse
CreateIncidentTypeResponse is the response of CreateIncidentType.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_type | [psim.incident.v1.IncidentType](#psim-incident-v1-IncidentType) |  |  |






<a name="psim-api-v1-GetIncidentRequest"></a>

### GetIncidentRequest
GetIncidentRequest is the request of GetIncident.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |






<a name="psim-api-v1-GetIncidentResponse"></a>

### GetIncidentResponse
GetIncidentResponse is the response of GetIncident.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-api-v1-IncidentSignal"></a>

### IncidentSignal
IncidentSignal is a signal attached to an incident.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| signal_id | [string](#string) |  |  |
| signal | [psim.processing.v1.Signal](#psim-processing-v1-Signal) |  |  |
| attached_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-api-v1-ListIncidentSignalsRequest"></a>

### ListIncidentSignalsRequest
ListIncidentSignalsRequest is the request of ListIncidentSignals.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListIncidentSignalsResponse"></a>

### ListIncidentSignalsResponse
ListIncidentSignalsResponse is the response of ListIncidentSignals.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| signals | [IncidentSignal](#psim-api-v1-IncidentSignal) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListIncidentTypesRequest"></a>

### ListIncidentTypesRequest
ListIncidentTypesRequest is the request of ListIncidentTypes.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListIncidentTypesResponse"></a>

### ListIncidentTypesResponse
ListIncidentTypesResponse is the response of ListIncidentTypes.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_types | [psim.incident.v1.IncidentType](#psim-incident-v1-IncidentType) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListIncidentsRequest"></a>

### ListIncidentsRequest
ListIncidentsRequest is the request of ListIncidents.


| Field | Type | Label | Description |
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
ListIncidentsResponse is the response of ListIncidents.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incidents | [psim.incident.v1.Incident](#psim-incident-v1-Incident) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListTimelineRequest"></a>

### ListTimelineRequest
ListTimelineRequest is the request of ListTimeline.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListTimelineResponse"></a>

### ListTimelineResponse
ListTimelineResponse is the response of ListTimeline.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| entries | [psim.incident.v1.TimelineEntry](#psim-incident-v1-TimelineEntry) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-ReopenIncidentRequest"></a>

### ReopenIncidentRequest
ReopenIncidentRequest is the request of ReopenIncident.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| reason | [string](#string) |  |  |






<a name="psim-api-v1-ReopenIncidentResponse"></a>

### ReopenIncidentResponse
ReopenIncidentResponse is the response of ReopenIncident.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-api-v1-ResolveIncidentRequest"></a>

### ResolveIncidentRequest
ResolveIncidentRequest is the request of ResolveIncident.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| resolution | [psim.incident.v1.Resolution](#psim-incident-v1-Resolution) |  |  |
| note | [string](#string) |  |  |
| known_response_run_version | [uint64](#uint64) |  | Response run version known to the client; guards against unseen mandatory steps (runtime-view R2). |






<a name="psim-api-v1-ResolveIncidentResponse"></a>

### ResolveIncidentResponse
ResolveIncidentResponse is the response of ResolveIncident.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-api-v1-SetIncidentPriorityRequest"></a>

### SetIncidentPriorityRequest
SetIncidentPriorityRequest is the request of SetIncidentPriority.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| priority | [psim.common.v1.Priority](#psim-common-v1-Priority) |  |  |
| reason | [string](#string) |  |  |






<a name="psim-api-v1-SetIncidentPriorityResponse"></a>

### SetIncidentPriorityResponse
SetIncidentPriorityResponse is the response of SetIncidentPriority.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-api-v1-StartIncidentWorkRequest"></a>

### StartIncidentWorkRequest
StartIncidentWorkRequest is the request of StartIncidentWork.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |






<a name="psim-api-v1-StartIncidentWorkResponse"></a>

### StartIncidentWorkResponse
StartIncidentWorkResponse is the response of StartIncidentWork.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [psim.incident.v1.Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-api-v1-UpdateIncidentTypeRequest"></a>

### UpdateIncidentTypeRequest
UpdateIncidentTypeRequest is the request of UpdateIncidentType.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_type_id | [string](#string) |  |  |
| incident_type | [psim.incident.v1.IncidentType](#psim-incident-v1-IncidentType) |  |  |






<a name="psim-api-v1-UpdateIncidentTypeResponse"></a>

### UpdateIncidentTypeResponse
UpdateIncidentTypeResponse is the response of UpdateIncidentType.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_type | [psim.incident.v1.IncidentType](#psim-incident-v1-IncidentType) |  |  |





 

 

 


<a name="psim-api-v1-IncidentService"></a>

### IncidentService
IncidentService manages incidents and incident types. Owner: Incident Service.
Scenarios S5-S7, S9. State transitions follow state-models.md, section 1.

| Method Name | Request Type | Response Type | Description |
| ----------- | ------------ | ------------- | ------------|
| ListIncidents | [ListIncidentsRequest](#psim-api-v1-ListIncidentsRequest) | [ListIncidentsResponse](#psim-api-v1-ListIncidentsResponse) | ListIncidents lists incidents within the caller scope. |
| GetIncident | [GetIncidentRequest](#psim-api-v1-GetIncidentRequest) | [GetIncidentResponse](#psim-api-v1-GetIncidentResponse) | GetIncident returns one incident; the ETag is the incident version. |
| ListTimeline | [ListTimelineRequest](#psim-api-v1-ListTimelineRequest) | [ListTimelineResponse](#psim-api-v1-ListTimelineResponse) | ListTimeline returns the incident timeline. |
| ListIncidentSignals | [ListIncidentSignalsRequest](#psim-api-v1-ListIncidentSignalsRequest) | [ListIncidentSignalsResponse](#psim-api-v1-ListIncidentSignalsResponse) | ListIncidentSignals returns signals attached to the incident. |
| AcknowledgeIncident | [AcknowledgeIncidentRequest](#psim-api-v1-AcknowledgeIncidentRequest) | [AcknowledgeIncidentResponse](#psim-api-v1-AcknowledgeIncidentResponse) | AcknowledgeIncident accepts an incident; the caller becomes the assignee. |
| StartIncidentWork | [StartIncidentWorkRequest](#psim-api-v1-StartIncidentWorkRequest) | [StartIncidentWorkResponse](#psim-api-v1-StartIncidentWorkResponse) | StartIncidentWork moves an incident to in_progress. |
| AssignIncident | [AssignIncidentRequest](#psim-api-v1-AssignIncidentRequest) | [AssignIncidentResponse](#psim-api-v1-AssignIncidentResponse) | AssignIncident assigns or reassigns an incident. |
| SetIncidentPriority | [SetIncidentPriorityRequest](#psim-api-v1-SetIncidentPriorityRequest) | [SetIncidentPriorityResponse](#psim-api-v1-SetIncidentPriorityResponse) | SetIncidentPriority sets the priority manually (supervisor). |
| AddIncidentComment | [AddIncidentCommentRequest](#psim-api-v1-AddIncidentCommentRequest) | [AddIncidentCommentResponse](#psim-api-v1-AddIncidentCommentResponse) | AddIncidentComment adds a comment to the timeline. |
| AddIncidentLink | [AddIncidentLinkRequest](#psim-api-v1-AddIncidentLinkRequest) | [AddIncidentLinkResponse](#psim-api-v1-AddIncidentLinkResponse) | AddIncidentLink attaches an external link. |
| ResolveIncident | [ResolveIncidentRequest](#psim-api-v1-ResolveIncidentRequest) | [ResolveIncidentResponse](#psim-api-v1-ResolveIncidentResponse) | ResolveIncident resolves an incident; fails while mandatory steps are pending (I7). |
| ReopenIncident | [ReopenIncidentRequest](#psim-api-v1-ReopenIncidentRequest) | [ReopenIncidentResponse](#psim-api-v1-ReopenIncidentResponse) | ReopenIncident reopens a resolved incident (I10). |
| CloseIncident | [CloseIncidentRequest](#psim-api-v1-CloseIncidentRequest) | [CloseIncidentResponse](#psim-api-v1-CloseIncidentResponse) | CloseIncident closes a resolved incident before the auto-close timer (supervisor). |
| ListIncidentTypes | [ListIncidentTypesRequest](#psim-api-v1-ListIncidentTypesRequest) | [ListIncidentTypesResponse](#psim-api-v1-ListIncidentTypesResponse) | ListIncidentTypes lists incident types. |
| CreateIncidentType | [CreateIncidentTypeRequest](#psim-api-v1-CreateIncidentTypeRequest) | [CreateIncidentTypeResponse](#psim-api-v1-CreateIncidentTypeResponse) | CreateIncidentType creates an incident type. |
| UpdateIncidentType | [UpdateIncidentTypeRequest](#psim-api-v1-UpdateIncidentTypeRequest) | [UpdateIncidentTypeResponse](#psim-api-v1-UpdateIncidentTypeResponse) | UpdateIncidentType replaces an incident type (If-Match required). |

 



<a name="psim_api_v1_me-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/api/v1/me.proto



<a name="psim-api-v1-GetMeRequest"></a>

### GetMeRequest
GetMeRequest is the request of GetMe.






<a name="psim-api-v1-GetMeResponse"></a>

### GetMeResponse
GetMeResponse is the response of GetMe.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| user_id | [string](#string) |  |  |
| tenant_id | [string](#string) |  |  |
| display_name | [string](#string) |  |  |
| email | [string](#string) |  |  |
| roles | [string](#string) | repeated | Roles: operator, supervisor, admin, integrator, auditor, manager. |
| site_ids | [string](#string) | repeated |  |
| zone_ids | [string](#string) | repeated |  |
| actions | [string](#string) | repeated | Actions allowed by the role table, for hiding unavailable UI actions (ADR-020). |
| locale | [string](#string) |  |  |





 

 

 


<a name="psim-api-v1-MeService"></a>

### MeService
MeService describes the caller. Owner: API Gateway (from the access token and the policy table).

| Method Name | Request Type | Response Type | Description |
| ----------- | ------------ | ------------- | ------------|
| GetMe | [GetMeRequest](#psim-api-v1-GetMeRequest) | [GetMeResponse](#psim-api-v1-GetMeResponse) | GetMe returns the caller identity, roles, scopes and allowed actions. |

 



<a name="psim_response_v1_plan-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/response/v1/plan.proto



<a name="psim-response-v1-AutomatedParams"></a>

### AutomatedParams
AutomatedParams calls an external handler (ADR-025).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| webhook_url | [string](#string) |  |  |
| timeout | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |






<a name="psim-response-v1-ChecklistItem"></a>

### ChecklistItem
ChecklistItem is one checklist entry.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| item_id | [string](#string) |  |  |
| text | [string](#string) |  |  |
| required | [bool](#bool) |  |  |






<a name="psim-response-v1-ChecklistParams"></a>

### ChecklistParams
ChecklistParams is a list of items to check.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| items | [ChecklistItem](#psim-response-v1-ChecklistItem) | repeated |  |






<a name="psim-response-v1-CommandParams"></a>

### CommandParams
CommandParams sends a command to devices (P3).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| capability | [string](#string) |  |  |
| device_id | [string](#string) |  |  |
| target_expression | [string](#string) |  | Target expression, for example &#34;zone.devices(type == &#39;access.door&#39;)&#34;. |
| parameters | [CommandParams.ParametersEntry](#psim-response-v1-CommandParams-ParametersEntry) | repeated |  |
| automatic | [bool](#bool) |  | Execute without operator confirmation. |






<a name="psim-response-v1-CommandParams-ParametersEntry"></a>

### CommandParams.ParametersEntry



| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-response-v1-DecisionOption"></a>

### DecisionOption
DecisionOption is one branch of a decision.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| option_id | [string](#string) |  |  |
| label | [string](#string) |  |  |
| next_step_id | [string](#string) |  |  |
| suggested_resolution | [string](#string) |  | Resolution suggested when this option leads to the end of the plan, for example false alarm. |






<a name="psim-response-v1-DecisionParams"></a>

### DecisionParams
DecisionParams branches the plan by the operator choice (P2).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| question | [string](#string) |  |  |
| options | [DecisionOption](#psim-response-v1-DecisionOption) | repeated |  |






<a name="psim-response-v1-EscalateParams"></a>

### EscalateParams
EscalateParams escalates the incident to the given level.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| level | [uint32](#uint32) |  |  |






<a name="psim-response-v1-EscalationLevel"></a>

### EscalationLevel
EscalationLevel is one level of escalation.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| level | [uint32](#uint32) |  |  |
| delay | [google.protobuf.Duration](#google-protobuf-Duration) |  | Delay after the SLA breach. |
| recipients | [Recipient](#psim-response-v1-Recipient) | repeated |  |
| channels | [NotificationChannel](#psim-response-v1-NotificationChannel) | repeated |  |






<a name="psim-response-v1-EscalationPolicy"></a>

### EscalationPolicy
EscalationPolicy defines SLA and escalation levels (aggregates.md, 5.4).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| policy_id | [string](#string) |  |  |
| name | [string](#string) |  |  |
| sla | [SlaTarget](#psim-response-v1-SlaTarget) | repeated |  |
| levels | [EscalationLevel](#psim-response-v1-EscalationLevel) | repeated | Strictly increasing delays (EP1). |
| version | [uint64](#uint64) |  |  |






<a name="psim-response-v1-InstructionParams"></a>

### InstructionParams
InstructionParams shows text the operator must read and confirm.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| text | [string](#string) |  |  |






<a name="psim-response-v1-NotifyParams"></a>

### NotifyParams
NotifyParams sends notifications.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| recipients | [Recipient](#psim-response-v1-Recipient) | repeated |  |
| channels | [NotificationChannel](#psim-response-v1-NotificationChannel) | repeated |  |
| template | [string](#string) |  |  |






<a name="psim-response-v1-PlanBinding"></a>

### PlanBinding
PlanBinding selects incidents a plan applies to; the most specific binding wins (P5).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_type_id | [string](#string) |  |  |
| site_ids | [string](#string) | repeated |  |
| zone_ids | [string](#string) | repeated |  |
| priorities | [psim.common.v1.Priority](#psim-common-v1-Priority) | repeated |  |
| binding_priority | [int32](#int32) |  | Tie-breaker among bindings with equal specificity. |






<a name="psim-response-v1-Recipient"></a>

### Recipient
Recipient is a notification recipient: a user or a role within the incident scope.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| user_id | [string](#string) |  |  |
| role | [string](#string) |  |  |






<a name="psim-response-v1-ResponsePlan"></a>

### ResponsePlan
ResponsePlan is one immutable version of a response plan (aggregates.md, 5.1).


| Field | Type | Label | Description |
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
SlaTarget is the SLA for one priority.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| priority | [psim.common.v1.Priority](#psim-common-v1-Priority) |  |  |
| acknowledge_within | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| resolve_within | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |






<a name="psim-response-v1-Step"></a>

### Step
Step is a node of a plan graph.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| step_id | [string](#string) |  |  |
| title | [string](#string) |  |  |
| mandatory | [bool](#bool) |  |  |
| next_step_id | [string](#string) |  | Next step for all kinds except decision; empty for a final step. |
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
WaitParams pauses the plan.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| duration | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |





 


<a name="psim-response-v1-NotificationChannel"></a>

### NotificationChannel
NotificationChannel is a delivery channel.

| Name | Number | Description |
| ---- | ------ | ----------- |
| NOTIFICATION_CHANNEL_UNSPECIFIED | 0 |  |
| NOTIFICATION_CHANNEL_IN_APP | 1 |  |
| NOTIFICATION_CHANNEL_EMAIL | 2 |  |
| NOTIFICATION_CHANNEL_WEBHOOK | 3 |  |



<a name="psim-response-v1-PlanState"></a>

### PlanState
PlanState is the lifecycle state of a plan version.

| Name | Number | Description |
| ---- | ------ | ----------- |
| PLAN_STATE_UNSPECIFIED | 0 |  |
| PLAN_STATE_DRAFT | 1 |  |
| PLAN_STATE_PUBLISHED | 2 |  |
| PLAN_STATE_ARCHIVED | 3 |  |


 

 

 



<a name="psim_response_v1_notification-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/response/v1/notification.proto



<a name="psim-response-v1-Notification"></a>

### Notification
Notification is a message to a recipient over a channel (aggregates.md, 5.6).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| notification_id | [string](#string) |  |  |
| recipient_user_id | [string](#string) |  |  |
| channel | [NotificationChannel](#psim-response-v1-NotificationChannel) |  |  |
| state | [NotificationState](#psim-response-v1-NotificationState) |  |  |
| template | [string](#string) |  |  |
| data | [Notification.DataEntry](#psim-response-v1-Notification-DataEntry) | repeated |  |
| attempts | [uint32](#uint32) |  |  |
| incident_id | [string](#string) |  |  |
| aggregated_notification_ids | [string](#string) | repeated | Set for aggregated notifications (N3). |
| created_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| delivered_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| read | [bool](#bool) |  | In-app notifications only. |
| version | [uint64](#uint64) |  |  |






<a name="psim-response-v1-Notification-DataEntry"></a>

### Notification.DataEntry



| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |





 


<a name="psim-response-v1-NotificationState"></a>

### NotificationState
NotificationState is the delivery state (state-models.md, 6).

| Name | Number | Description |
| ---- | ------ | ----------- |
| NOTIFICATION_STATE_UNSPECIFIED | 0 |  |
| NOTIFICATION_STATE_PENDING | 1 |  |
| NOTIFICATION_STATE_SENT | 2 |  |
| NOTIFICATION_STATE_DELIVERED | 3 |  |
| NOTIFICATION_STATE_FAILED | 4 |  |
| NOTIFICATION_STATE_AGGREGATED | 5 |  |


 

 

 



<a name="psim_api_v1_notifications-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/api/v1/notifications.proto



<a name="psim-api-v1-ListMyNotificationsRequest"></a>

### ListMyNotificationsRequest
ListMyNotificationsRequest is the request of ListMyNotifications.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| unread_only | [bool](#bool) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListMyNotificationsResponse"></a>

### ListMyNotificationsResponse
ListMyNotificationsResponse is the response of ListMyNotifications.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| notifications | [psim.response.v1.Notification](#psim-response-v1-Notification) | repeated |  |
| next_page_token | [string](#string) |  |  |
| unread_count | [uint32](#uint32) |  |  |






<a name="psim-api-v1-MarkNotificationReadRequest"></a>

### MarkNotificationReadRequest
MarkNotificationReadRequest is the request of MarkNotificationRead.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| notification_id | [string](#string) |  |  |






<a name="psim-api-v1-MarkNotificationReadResponse"></a>

### MarkNotificationReadResponse
MarkNotificationReadResponse is the response of MarkNotificationRead.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| notification | [psim.response.v1.Notification](#psim-response-v1-Notification) |  |  |





 

 

 


<a name="psim-api-v1-NotificationService"></a>

### NotificationService
NotificationService serves in-app notifications of the caller. Owner: Notification Service. Scenario S9.

| Method Name | Request Type | Response Type | Description |
| ----------- | ------------ | ------------- | ------------|
| ListMyNotifications | [ListMyNotificationsRequest](#psim-api-v1-ListMyNotificationsRequest) | [ListMyNotificationsResponse](#psim-api-v1-ListMyNotificationsResponse) | ListMyNotifications lists in-app notifications of the caller; newest first. |
| MarkNotificationRead | [MarkNotificationReadRequest](#psim-api-v1-MarkNotificationReadRequest) | [MarkNotificationReadResponse](#psim-api-v1-MarkNotificationReadResponse) | MarkNotificationRead marks an in-app notification as read. |

 



<a name="psim_api_v1_problem-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/api/v1/problem.proto



<a name="psim-api-v1-FieldViolation"></a>

### FieldViolation
FieldViolation describes one invalid field.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| field | [string](#string) |  | Field path in the request, for example &#34;steps[2].next_step_id&#34;. |
| code | [string](#string) |  |  |
| message | [string](#string) |  |  |






<a name="psim-api-v1-Problem"></a>

### Problem
Problem is the error body of every failed REST request (RFC 9457).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| type | [string](#string) |  | URI of the error type: https://psim.local/errors/&lt;code&gt;. |
| title | [string](#string) |  |  |
| status | [int32](#int32) |  |  |
| detail | [string](#string) |  |  |
| instance | [string](#string) |  |  |
| code | [string](#string) |  | Error code from contracts/errors/errors.yaml. |
| trace_id | [string](#string) |  |  |
| errors | [FieldViolation](#psim-api-v1-FieldViolation) | repeated |  |





 

 

 

 



<a name="psim_api_v1_reports-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/api/v1/reports.proto



<a name="psim-api-v1-ExportIncidentReportRequest"></a>

### ExportIncidentReportRequest
ExportIncidentReportRequest is the request of ExportIncidentReport.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| filter | [ReportFilter](#psim-api-v1-ReportFilter) |  |  |
| grouping | [ReportGrouping](#psim-api-v1-ReportGrouping) |  |  |






<a name="psim-api-v1-ExportIncidentReportResponse"></a>

### ExportIncidentReportResponse
ExportIncidentReportResponse is the response of ExportIncidentReport; the body is CSV.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| file | [google.api.HttpBody](#google-api-HttpBody) |  |  |






<a name="psim-api-v1-GetIncidentReportRequest"></a>

### GetIncidentReportRequest
GetIncidentReportRequest is the request of GetIncidentReport.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| filter | [ReportFilter](#psim-api-v1-ReportFilter) |  |  |
| grouping | [ReportGrouping](#psim-api-v1-ReportGrouping) |  |  |






<a name="psim-api-v1-GetIncidentReportResponse"></a>

### GetIncidentReportResponse
GetIncidentReportResponse is the response of GetIncidentReport.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| totals | [IncidentMetrics](#psim-api-v1-IncidentMetrics) |  |  |
| rows | [IncidentMetrics](#psim-api-v1-IncidentMetrics) | repeated |  |
| generated_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-api-v1-IncidentMetrics"></a>

### IncidentMetrics
IncidentMetrics are incident metrics for a group (definitions in acceptance-spec.md, S13).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| group | [string](#string) |  | Group key: site id, incident type id, priority, user id or date; empty for totals. |
| incidents_created | [uint64](#uint64) |  |  |
| incidents_resolved | [uint64](#uint64) |  |  |
| mtta | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| mttr | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| false_alarm_rate | [double](#double) |  | Share of resolution = false_alarm among resolved, 0..1. |
| ack_sla_compliance | [double](#double) |  | Share of incidents acknowledged within SLA, 0..1. |
| resolve_sla_compliance | [double](#double) |  | Share of incidents resolved within SLA, 0..1. |
| escalated | [uint64](#uint64) |  |  |






<a name="psim-api-v1-ReportFilter"></a>

### ReportFilter
ReportFilter selects incidents for a report.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| from | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| to | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| site_ids | [string](#string) | repeated |  |
| incident_type_ids | [string](#string) | repeated |  |
| priorities | [psim.common.v1.Priority](#psim-common-v1-Priority) | repeated |  |





 


<a name="psim-api-v1-ReportGrouping"></a>

### ReportGrouping
ReportGrouping is the grouping dimension of report rows.

| Name | Number | Description |
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
ReportService builds incident reports (ADR-028: analytics in ClickHouse).
Owner: Projection Service. Scenario S13.

| Method Name | Request Type | Response Type | Description |
| ----------- | ------------ | ------------- | ------------|
| GetIncidentReport | [GetIncidentReportRequest](#psim-api-v1-GetIncidentReportRequest) | [GetIncidentReportResponse](#psim-api-v1-GetIncidentReportResponse) | GetIncidentReport returns incident metrics for a period. |
| ExportIncidentReport | [ExportIncidentReportRequest](#psim-api-v1-ExportIncidentReportRequest) | [ExportIncidentReportResponse](#psim-api-v1-ExportIncidentReportResponse) | ExportIncidentReport exports the same report as CSV. |

 



<a name="psim_response_v1_run-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/response/v1/run.proto



<a name="psim-response-v1-ResponseRun"></a>

### ResponseRun
ResponseRun is the execution of a plan version for an incident (aggregates.md, 5.2).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| run_id | [string](#string) |  |  |
| incident_id | [string](#string) |  |  |
| plan_id | [string](#string) |  |  |
| plan_version | [uint32](#uint32) |  |  |
| state | [RunState](#psim-response-v1-RunState) |  |  |
| steps | [StepExecution](#psim-response-v1-StepExecution) | repeated |  |
| active_step_ids | [string](#string) | repeated |  |
| blocking_step_ids | [string](#string) | repeated | Unfinished mandatory steps (RR5). |
| started_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| finished_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| version | [uint64](#uint64) |  |  |






<a name="psim-response-v1-StepExecution"></a>

### StepExecution
StepExecution is the execution state of one step.


| Field | Type | Label | Description |
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
StepResult is what the step produced.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| checked_item_ids | [string](#string) | repeated |  |
| decision_option_id | [string](#string) |  |  |
| command_ids | [string](#string) | repeated |  |
| note | [string](#string) |  | Reason for skip or override, or a free note. |
| overridden | [bool](#bool) |  | Set when a supervisor completed a failed step (Override). |





 


<a name="psim-response-v1-RunState"></a>

### RunState
RunState is the lifecycle state of a run (state-models.md, 2.1).

| Name | Number | Description |
| ---- | ------ | ----------- |
| RUN_STATE_UNSPECIFIED | 0 |  |
| RUN_STATE_RUNNING | 1 |  |
| RUN_STATE_COMPLETED | 2 |  |
| RUN_STATE_CANCELLED | 3 |  |



<a name="psim-response-v1-StepState"></a>

### StepState
StepState is the state of a step (state-models.md, 2.2).

| Name | Number | Description |
| ---- | ------ | ----------- |
| STEP_STATE_UNSPECIFIED | 0 |  |
| STEP_STATE_PENDING | 1 |  |
| STEP_STATE_ACTIVE | 2 |  |
| STEP_STATE_COMPLETED | 3 |  |
| STEP_STATE_SKIPPED | 4 |  |
| STEP_STATE_FAILED | 5 |  |
| STEP_STATE_CANCELLED | 6 |  |


 

 

 



<a name="psim_api_v1_response-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/api/v1/response.proto



<a name="psim-api-v1-ArchivePlanRequest"></a>

### ArchivePlanRequest
ArchivePlanRequest is the request of ArchivePlan.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| plan_id | [string](#string) |  |  |






<a name="psim-api-v1-ArchivePlanResponse"></a>

### ArchivePlanResponse
ArchivePlanResponse is the response of ArchivePlan.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| plan | [psim.response.v1.ResponsePlan](#psim-response-v1-ResponsePlan) |  |  |






<a name="psim-api-v1-CompleteStepRequest"></a>

### CompleteStepRequest
CompleteStepRequest is the request of CompleteStep.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| run_id | [string](#string) |  |  |
| step_id | [string](#string) |  |  |
| result | [psim.response.v1.StepResult](#psim-response-v1-StepResult) |  |  |






<a name="psim-api-v1-CompleteStepResponse"></a>

### CompleteStepResponse
CompleteStepResponse is the response of CompleteStep.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| run | [psim.response.v1.ResponseRun](#psim-response-v1-ResponseRun) |  |  |






<a name="psim-api-v1-CreateEscalationPolicyRequest"></a>

### CreateEscalationPolicyRequest
CreateEscalationPolicyRequest is the request of CreateEscalationPolicy.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| policy | [psim.response.v1.EscalationPolicy](#psim-response-v1-EscalationPolicy) |  |  |






<a name="psim-api-v1-CreateEscalationPolicyResponse"></a>

### CreateEscalationPolicyResponse
CreateEscalationPolicyResponse is the response of CreateEscalationPolicy.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| policy | [psim.response.v1.EscalationPolicy](#psim-response-v1-EscalationPolicy) |  |  |






<a name="psim-api-v1-GetIncidentRunRequest"></a>

### GetIncidentRunRequest
GetIncidentRunRequest is the request of GetIncidentRun.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |






<a name="psim-api-v1-GetIncidentRunResponse"></a>

### GetIncidentRunResponse
GetIncidentRunResponse is the response of GetIncidentRun.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| run | [psim.response.v1.ResponseRun](#psim-response-v1-ResponseRun) |  |  |
| plan | [psim.response.v1.ResponsePlan](#psim-response-v1-ResponsePlan) |  | The plan version used by the run. |






<a name="psim-api-v1-GetPlanRequest"></a>

### GetPlanRequest
GetPlanRequest is the request of GetPlan.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| plan_id | [string](#string) |  |  |
| version | [uint32](#uint32) |  |  |






<a name="psim-api-v1-GetPlanResponse"></a>

### GetPlanResponse
GetPlanResponse is the response of GetPlan.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| plan | [psim.response.v1.ResponsePlan](#psim-response-v1-ResponsePlan) |  |  |






<a name="psim-api-v1-ListEscalationPoliciesRequest"></a>

### ListEscalationPoliciesRequest
ListEscalationPoliciesRequest is the request of ListEscalationPolicies.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListEscalationPoliciesResponse"></a>

### ListEscalationPoliciesResponse
ListEscalationPoliciesResponse is the response of ListEscalationPolicies.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| policies | [psim.response.v1.EscalationPolicy](#psim-response-v1-EscalationPolicy) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListPlansRequest"></a>

### ListPlansRequest
ListPlansRequest is the request of ListPlans.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_type_id | [string](#string) |  |  |
| include_archived | [bool](#bool) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListPlansResponse"></a>

### ListPlansResponse
ListPlansResponse is the response of ListPlans.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| plans | [psim.response.v1.ResponsePlan](#psim-response-v1-ResponsePlan) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-OverrideStepRequest"></a>

### OverrideStepRequest
OverrideStepRequest is the request of OverrideStep.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| run_id | [string](#string) |  |  |
| step_id | [string](#string) |  |  |
| reason | [string](#string) |  |  |






<a name="psim-api-v1-OverrideStepResponse"></a>

### OverrideStepResponse
OverrideStepResponse is the response of OverrideStep.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| run | [psim.response.v1.ResponseRun](#psim-response-v1-ResponseRun) |  |  |






<a name="psim-api-v1-PublishPlanRequest"></a>

### PublishPlanRequest
PublishPlanRequest is the request of PublishPlan.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| plan_id | [string](#string) |  |  |
| version | [uint32](#uint32) |  |  |






<a name="psim-api-v1-PublishPlanResponse"></a>

### PublishPlanResponse
PublishPlanResponse is the response of PublishPlan.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| plan | [psim.response.v1.ResponsePlan](#psim-response-v1-ResponsePlan) |  |  |






<a name="psim-api-v1-RetryStepRequest"></a>

### RetryStepRequest
RetryStepRequest is the request of RetryStep.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| run_id | [string](#string) |  |  |
| step_id | [string](#string) |  |  |






<a name="psim-api-v1-RetryStepResponse"></a>

### RetryStepResponse
RetryStepResponse is the response of RetryStep.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| run | [psim.response.v1.ResponseRun](#psim-response-v1-ResponseRun) |  |  |






<a name="psim-api-v1-SavePlanDraftRequest"></a>

### SavePlanDraftRequest
SavePlanDraftRequest is the request of SavePlanDraft.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| plan | [psim.response.v1.ResponsePlan](#psim-response-v1-ResponsePlan) |  |  |






<a name="psim-api-v1-SavePlanDraftResponse"></a>

### SavePlanDraftResponse
SavePlanDraftResponse is the response of SavePlanDraft.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| plan | [psim.response.v1.ResponsePlan](#psim-response-v1-ResponsePlan) |  |  |






<a name="psim-api-v1-SkipStepRequest"></a>

### SkipStepRequest
SkipStepRequest is the request of SkipStep.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| run_id | [string](#string) |  |  |
| step_id | [string](#string) |  |  |
| reason | [string](#string) |  |  |






<a name="psim-api-v1-SkipStepResponse"></a>

### SkipStepResponse
SkipStepResponse is the response of SkipStep.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| run | [psim.response.v1.ResponseRun](#psim-response-v1-ResponseRun) |  |  |






<a name="psim-api-v1-UpdateEscalationPolicyRequest"></a>

### UpdateEscalationPolicyRequest
UpdateEscalationPolicyRequest is the request of UpdateEscalationPolicy.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| policy_id | [string](#string) |  |  |
| policy | [psim.response.v1.EscalationPolicy](#psim-response-v1-EscalationPolicy) |  |  |






<a name="psim-api-v1-UpdateEscalationPolicyResponse"></a>

### UpdateEscalationPolicyResponse
UpdateEscalationPolicyResponse is the response of UpdateEscalationPolicy.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| policy | [psim.response.v1.EscalationPolicy](#psim-response-v1-EscalationPolicy) |  |  |






<a name="psim-api-v1-ValidatePlanRequest"></a>

### ValidatePlanRequest
ValidatePlanRequest is the request of ValidatePlan.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| plan | [psim.response.v1.ResponsePlan](#psim-response-v1-ResponsePlan) |  |  |






<a name="psim-api-v1-ValidatePlanResponse"></a>

### ValidatePlanResponse
ValidatePlanResponse is the response of ValidatePlan.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| valid | [bool](#bool) |  |  |
| violations | [FieldViolation](#psim-api-v1-FieldViolation) | repeated |  |





 

 

 


<a name="psim-api-v1-EscalationPolicyService"></a>

### EscalationPolicyService
EscalationPolicyService manages SLA and escalation policies. Owner: Response Engine. Scenario S9.

| Method Name | Request Type | Response Type | Description |
| ----------- | ------------ | ------------- | ------------|
| ListEscalationPolicies | [ListEscalationPoliciesRequest](#psim-api-v1-ListEscalationPoliciesRequest) | [ListEscalationPoliciesResponse](#psim-api-v1-ListEscalationPoliciesResponse) | ListEscalationPolicies lists policies. |
| CreateEscalationPolicy | [CreateEscalationPolicyRequest](#psim-api-v1-CreateEscalationPolicyRequest) | [CreateEscalationPolicyResponse](#psim-api-v1-CreateEscalationPolicyResponse) | CreateEscalationPolicy creates a policy. |
| UpdateEscalationPolicy | [UpdateEscalationPolicyRequest](#psim-api-v1-UpdateEscalationPolicyRequest) | [UpdateEscalationPolicyResponse](#psim-api-v1-UpdateEscalationPolicyResponse) | UpdateEscalationPolicy replaces a policy (If-Match required). |


<a name="psim-api-v1-ResponsePlanService"></a>

### ResponsePlanService
ResponsePlanService manages response plans. Owner: Response Engine. Scenario S7.

| Method Name | Request Type | Response Type | Description |
| ----------- | ------------ | ------------- | ------------|
| ListPlans | [ListPlansRequest](#psim-api-v1-ListPlansRequest) | [ListPlansResponse](#psim-api-v1-ListPlansResponse) | ListPlans lists the latest version of each plan. |
| GetPlan | [GetPlanRequest](#psim-api-v1-GetPlanRequest) | [GetPlanResponse](#psim-api-v1-GetPlanResponse) | GetPlan returns a plan version; latest when version is 0. |
| SavePlanDraft | [SavePlanDraftRequest](#psim-api-v1-SavePlanDraftRequest) | [SavePlanDraftResponse](#psim-api-v1-SavePlanDraftResponse) | SavePlanDraft creates a plan or a new draft version of it. |
| ValidatePlan | [ValidatePlanRequest](#psim-api-v1-ValidatePlanRequest) | [ValidatePlanResponse](#psim-api-v1-ValidatePlanResponse) | ValidatePlan validates a plan without saving it (P1-P5). |
| PublishPlan | [PublishPlanRequest](#psim-api-v1-PublishPlanRequest) | [PublishPlanResponse](#psim-api-v1-PublishPlanResponse) | PublishPlan publishes a draft version; published versions are immutable. |
| ArchivePlan | [ArchivePlanRequest](#psim-api-v1-ArchivePlanRequest) | [ArchivePlanResponse](#psim-api-v1-ArchivePlanResponse) | ArchivePlan archives a plan. |


<a name="psim-api-v1-ResponseRunService"></a>

### ResponseRunService
ResponseRunService executes plan steps. Owner: Response Engine. Scenario S7.

| Method Name | Request Type | Response Type | Description |
| ----------- | ------------ | ------------- | ------------|
| GetIncidentRun | [GetIncidentRunRequest](#psim-api-v1-GetIncidentRunRequest) | [GetIncidentRunResponse](#psim-api-v1-GetIncidentRunResponse) | GetIncidentRun returns the active or last run of an incident. |
| CompleteStep | [CompleteStepRequest](#psim-api-v1-CompleteStepRequest) | [CompleteStepResponse](#psim-api-v1-CompleteStepResponse) | CompleteStep completes an active step with its result. |
| SkipStep | [SkipStepRequest](#psim-api-v1-SkipStepRequest) | [SkipStepResponse](#psim-api-v1-SkipStepResponse) | SkipStep skips a non-mandatory step with a reason (RR3). |
| RetryStep | [RetryStepRequest](#psim-api-v1-RetryStepRequest) | [RetryStepResponse](#psim-api-v1-RetryStepResponse) | RetryStep retries a failed step. |
| OverrideStep | [OverrideStepRequest](#psim-api-v1-OverrideStepRequest) | [OverrideStepResponse](#psim-api-v1-OverrideStepResponse) | OverrideStep completes a failed step by a supervisor decision. |

 



<a name="psim_processing_v1_mapping-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/processing/v1/mapping.proto



<a name="psim-processing-v1-MappingRule"></a>

### MappingRule
MappingRule maps a raw event to a taxonomy type.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| raw_code | [string](#string) |  | Exact vendor code or prefix ending with &#34;*&#34;. |
| condition | [string](#string) |  | Optional CEL condition over the variable &#34;raw&#34;. |
| type | [string](#string) |  | Target taxonomy type code. |
| severity | [psim.common.v1.Severity](#psim-common-v1-Severity) |  | Overrides the default severity of the type when set. |
| attribute_map | [MappingRule.AttributeMapEntry](#psim-processing-v1-MappingRule-AttributeMapEntry) | repeated | Attribute renames: raw attribute name -&gt; normalized attribute name. |






<a name="psim-processing-v1-MappingRule-AttributeMapEntry"></a>

### MappingRule.AttributeMapEntry



| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-processing-v1-MappingRuleSet"></a>

### MappingRuleSet
MappingRuleSet is a published version of mapping rules for one connector type
(aggregates.md, 3.1). Published in psim.config.v1 under key &#34;mapping/&lt;tenant_id&gt;/&lt;connector_type&gt;&#34;.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| mapping_rule_set_id | [string](#string) |  |  |
| version | [uint32](#uint32) |  |  |
| connector_type | [string](#string) |  |  |
| taxonomy_version | [string](#string) |  |  |
| rules | [MappingRule](#psim-processing-v1-MappingRule) | repeated | Ordered; the first matching rule wins. |
| published_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| published_by | [psim.common.v1.Actor](#psim-common-v1-Actor) |  |  |





 

 

 

 



<a name="psim_processing_v1_rules-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/processing/v1/rules.proto



<a name="psim-processing-v1-AbsenceSpec"></a>

### AbsenceSpec
AbsenceSpec fires when an expected event does not follow a trigger within the window.
Without a trigger it fires when no expected event occurs within each window (heartbeat).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| trigger | [EventFilter](#psim-processing-v1-EventFilter) |  |  |
| expected | [EventFilter](#psim-processing-v1-EventFilter) |  |  |






<a name="psim-processing-v1-ConjunctionSpec"></a>

### ConjunctionSpec
ConjunctionSpec fires when every part matched within the window, in any order.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| parts | [EventFilter](#psim-processing-v1-EventFilter) | repeated |  |






<a name="psim-processing-v1-CorrelationRule"></a>

### CorrelationRule
CorrelationRule is one immutable version of a correlation rule (aggregates.md, 3.3; ADR-011).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| rule_id | [string](#string) |  |  |
| version | [uint32](#uint32) |  |  |
| name | [string](#string) |  |  |
| description | [string](#string) |  |  |
| state | [RuleState](#psim-processing-v1-RuleState) |  |  |
| filter | [EventFilter](#psim-processing-v1-EventFilter) |  | Events considered by the rule at all. |
| correlation_scope | [CorrelationScope](#psim-processing-v1-CorrelationScope) |  |  |
| window | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| allowed_lateness | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| suppression | [google.protobuf.Duration](#google-protobuf-Duration) |  | Suppress repeated signals for the same correlation key for this interval. |
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
EventFilter selects events by taxonomy type, severity, location and a CEL condition.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| types | [string](#string) | repeated | Taxonomy codes or prefixes ending with &#34;.*&#34;, for example &#34;intrusion.*&#34;. |
| min_severity | [psim.common.v1.Severity](#psim-common-v1-Severity) |  |  |
| site_ids | [string](#string) | repeated |  |
| zone_ids | [string](#string) | repeated |  |
| condition | [string](#string) |  | Optional CEL expression over the variable &#34;event&#34;. |






<a name="psim-processing-v1-RuleOutput"></a>

### RuleOutput
RuleOutput describes the signal produced by a rule.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_type_id | [string](#string) |  |  |
| priority | [psim.common.v1.Priority](#psim-common-v1-Priority) |  | Fixed priority, used when priority_expression is empty. |
| priority_expression | [string](#string) |  | Optional CEL expression returning 1..4. |
| grouping_scope | [CorrelationScope](#psim-processing-v1-CorrelationScope) |  | Scope of the grouping key; defaults to the correlation scope. |
| summary_template | [string](#string) |  | Summary template, may reference event and rule fields. |






<a name="psim-processing-v1-RuleSet"></a>

### RuleSet
RuleSet is the set of rule versions active for a tenant (aggregates.md, 3.4).
Published in psim.config.v1 under key &#34;ruleset/&lt;tenant_id&gt;&#34;.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| rule_set_id | [string](#string) |  |  |
| version | [uint64](#uint64) |  |  |
| rules | [CorrelationRule](#psim-processing-v1-CorrelationRule) | repeated |  |
| activated_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| activated_by | [psim.common.v1.Actor](#psim-common-v1-Actor) |  |  |






<a name="psim-processing-v1-SequenceSpec"></a>

### SequenceSpec
SequenceSpec fires when the steps matched within the window in the given order.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| steps | [EventFilter](#psim-processing-v1-EventFilter) | repeated |  |






<a name="psim-processing-v1-SingleSpec"></a>

### SingleSpec
SingleSpec fires on every matching event.






<a name="psim-processing-v1-ThresholdSpec"></a>

### ThresholdSpec
ThresholdSpec fires when at least count matching events occur within the window.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| count | [uint32](#uint32) |  |  |
| distinct_by | [string](#string) |  | Count distinct values of this field (&#34;device_id&#34;, &#34;source_id&#34; or an attribute) instead of events. |





 


<a name="psim-processing-v1-CorrelationScope"></a>

### CorrelationScope
CorrelationScope defines the correlation key of a rule (scaling.md, 3).

| Name | Number | Description |
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
RuleState is the lifecycle state of a rule version (state-models.md, 5).

| Name | Number | Description |
| ---- | ------ | ----------- |
| RULE_STATE_UNSPECIFIED | 0 |  |
| RULE_STATE_DRAFT | 1 |  |
| RULE_STATE_TESTING | 2 |  |
| RULE_STATE_ACTIVE | 3 |  |
| RULE_STATE_ARCHIVED | 4 |  |


 

 

 



<a name="psim_api_v1_rules-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/api/v1/rules.proto



<a name="psim-api-v1-ActivateRuleSetRequest"></a>

### ActivateRuleSetRequest
ActivateRuleSetRequest is the request of ActivateRuleSet.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| rules | [RuleVersionRef](#psim-api-v1-RuleVersionRef) | repeated | Rule versions to make active; every version must be in testing or active state. |
| expected_rule_set_version | [uint64](#uint64) |  | Version of the currently active set, for optimistic concurrency. |






<a name="psim-api-v1-ActivateRuleSetResponse"></a>

### ActivateRuleSetResponse
ActivateRuleSetResponse is the response of ActivateRuleSet.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| rule_set | [psim.processing.v1.RuleSet](#psim-processing-v1-RuleSet) |  |  |






<a name="psim-api-v1-ArchiveRuleRequest"></a>

### ArchiveRuleRequest
ArchiveRuleRequest is the request of ArchiveRule.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| rule_id | [string](#string) |  |  |






<a name="psim-api-v1-ArchiveRuleResponse"></a>

### ArchiveRuleResponse
ArchiveRuleResponse is the response of ArchiveRule.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| rule | [psim.processing.v1.CorrelationRule](#psim-processing-v1-CorrelationRule) |  |  |






<a name="psim-api-v1-DryRun"></a>

### DryRun
DryRun is a dry run of a rule over history.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| dry_run_id | [string](#string) |  |  |
| rule_id | [string](#string) |  |  |
| version | [uint32](#uint32) |  |  |
| state | [DryRunState](#psim-api-v1-DryRunState) |  |  |
| events_processed | [uint64](#uint64) |  |  |
| signals_produced | [uint64](#uint64) |  |  |
| started_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| finished_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-api-v1-GetActiveRuleSetRequest"></a>

### GetActiveRuleSetRequest
GetActiveRuleSetRequest is the request of GetActiveRuleSet.






<a name="psim-api-v1-GetActiveRuleSetResponse"></a>

### GetActiveRuleSetResponse
GetActiveRuleSetResponse is the response of GetActiveRuleSet.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| rule_set | [psim.processing.v1.RuleSet](#psim-processing-v1-RuleSet) |  |  |






<a name="psim-api-v1-GetDryRunRequest"></a>

### GetDryRunRequest
GetDryRunRequest is the request of GetDryRun.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| dry_run_id | [string](#string) |  |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-GetDryRunResponse"></a>

### GetDryRunResponse
GetDryRunResponse is the response of GetDryRun.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| dry_run | [DryRun](#psim-api-v1-DryRun) |  |  |
| signals | [psim.processing.v1.Signal](#psim-processing-v1-Signal) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-GetMappingRuleSetRequest"></a>

### GetMappingRuleSetRequest
GetMappingRuleSetRequest is the request of GetMappingRuleSet.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| connector_type | [string](#string) |  |  |






<a name="psim-api-v1-GetMappingRuleSetResponse"></a>

### GetMappingRuleSetResponse
GetMappingRuleSetResponse is the response of GetMappingRuleSet.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| mapping_rule_set | [psim.processing.v1.MappingRuleSet](#psim-processing-v1-MappingRuleSet) |  |  |






<a name="psim-api-v1-GetRuleRequest"></a>

### GetRuleRequest
GetRuleRequest is the request of GetRule.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| rule_id | [string](#string) |  |  |
| version | [uint32](#uint32) |  |  |






<a name="psim-api-v1-GetRuleResponse"></a>

### GetRuleResponse
GetRuleResponse is the response of GetRule.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| rule | [psim.processing.v1.CorrelationRule](#psim-processing-v1-CorrelationRule) |  |  |






<a name="psim-api-v1-ListMappingRuleSetsRequest"></a>

### ListMappingRuleSetsRequest
ListMappingRuleSetsRequest is the request of ListMappingRuleSets.






<a name="psim-api-v1-ListMappingRuleSetsResponse"></a>

### ListMappingRuleSetsResponse
ListMappingRuleSetsResponse is the response of ListMappingRuleSets.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| mapping_rule_sets | [psim.processing.v1.MappingRuleSet](#psim-processing-v1-MappingRuleSet) | repeated |  |






<a name="psim-api-v1-ListRulesRequest"></a>

### ListRulesRequest
ListRulesRequest is the request of ListRules.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| states | [psim.processing.v1.RuleState](#psim-processing-v1-RuleState) | repeated |  |
| page_size | [int32](#int32) |  |  |
| page_token | [string](#string) |  |  |






<a name="psim-api-v1-ListRulesResponse"></a>

### ListRulesResponse
ListRulesResponse is the response of ListRules.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| rules | [psim.processing.v1.CorrelationRule](#psim-processing-v1-CorrelationRule) | repeated |  |
| next_page_token | [string](#string) |  |  |






<a name="psim-api-v1-PublishMappingRuleSetRequest"></a>

### PublishMappingRuleSetRequest
PublishMappingRuleSetRequest is the request of PublishMappingRuleSet.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| connector_type | [string](#string) |  |  |
| mapping_rule_set | [psim.processing.v1.MappingRuleSet](#psim-processing-v1-MappingRuleSet) |  |  |






<a name="psim-api-v1-PublishMappingRuleSetResponse"></a>

### PublishMappingRuleSetResponse
PublishMappingRuleSetResponse is the response of PublishMappingRuleSet.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| mapping_rule_set | [psim.processing.v1.MappingRuleSet](#psim-processing-v1-MappingRuleSet) |  |  |






<a name="psim-api-v1-RuleVersionRef"></a>

### RuleVersionRef
RuleVersionRef references a rule version.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| rule_id | [string](#string) |  |  |
| version | [uint32](#uint32) |  |  |






<a name="psim-api-v1-SaveRuleDraftRequest"></a>

### SaveRuleDraftRequest
SaveRuleDraftRequest is the request of SaveRuleDraft.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| rule | [psim.processing.v1.CorrelationRule](#psim-processing-v1-CorrelationRule) |  |  |






<a name="psim-api-v1-SaveRuleDraftResponse"></a>

### SaveRuleDraftResponse
SaveRuleDraftResponse is the response of SaveRuleDraft.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| rule | [psim.processing.v1.CorrelationRule](#psim-processing-v1-CorrelationRule) |  |  |






<a name="psim-api-v1-StartDryRunRequest"></a>

### StartDryRunRequest
StartDryRunRequest is the request of StartDryRun.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| rule_id | [string](#string) |  |  |
| version | [uint32](#uint32) |  |  |
| from | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  | Range of event time, limited by the retention of normalized events. |
| to | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-api-v1-StartDryRunResponse"></a>

### StartDryRunResponse
StartDryRunResponse is the response of StartDryRun.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| dry_run | [DryRun](#psim-api-v1-DryRun) |  |  |






<a name="psim-api-v1-ValidateRuleRequest"></a>

### ValidateRuleRequest
ValidateRuleRequest is the request of ValidateRule.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| rule_id | [string](#string) |  |  |
| version | [uint32](#uint32) |  |  |






<a name="psim-api-v1-ValidateRuleResponse"></a>

### ValidateRuleResponse
ValidateRuleResponse is the response of ValidateRule.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| valid | [bool](#bool) |  |  |
| violations | [FieldViolation](#psim-api-v1-FieldViolation) | repeated |  |
| rule | [psim.processing.v1.CorrelationRule](#psim-processing-v1-CorrelationRule) |  |  |





 


<a name="psim-api-v1-DryRunState"></a>

### DryRunState
DryRunState is the state of a dry run.

| Name | Number | Description |
| ---- | ------ | ----------- |
| DRY_RUN_STATE_UNSPECIFIED | 0 |  |
| DRY_RUN_STATE_RUNNING | 1 |  |
| DRY_RUN_STATE_COMPLETED | 2 |  |
| DRY_RUN_STATE_FAILED | 3 |  |


 

 


<a name="psim-api-v1-CorrelationRuleService"></a>

### CorrelationRuleService
CorrelationRuleService manages correlation rules and the active rule set.
Owner: Correlation Engine (rule management module). Scenario S4.

| Method Name | Request Type | Response Type | Description |
| ----------- | ------------ | ------------- | ------------|
| ListRules | [ListRulesRequest](#psim-api-v1-ListRulesRequest) | [ListRulesResponse](#psim-api-v1-ListRulesResponse) | ListRules lists the latest version of each rule. |
| GetRule | [GetRuleRequest](#psim-api-v1-GetRuleRequest) | [GetRuleResponse](#psim-api-v1-GetRuleResponse) | GetRule returns a rule version; latest when version is 0. |
| SaveRuleDraft | [SaveRuleDraftRequest](#psim-api-v1-SaveRuleDraftRequest) | [SaveRuleDraftResponse](#psim-api-v1-SaveRuleDraftResponse) | SaveRuleDraft creates a rule or a new draft version of it. |
| ValidateRule | [ValidateRuleRequest](#psim-api-v1-ValidateRuleRequest) | [ValidateRuleResponse](#psim-api-v1-ValidateRuleResponse) | ValidateRule validates a draft and moves it to testing on success (R1). |
| StartDryRun | [StartDryRunRequest](#psim-api-v1-StartDryRunRequest) | [StartDryRunResponse](#psim-api-v1-StartDryRunResponse) | StartDryRun runs a rule version over historical events without publishing signals. |
| GetDryRun | [GetDryRunRequest](#psim-api-v1-GetDryRunRequest) | [GetDryRunResponse](#psim-api-v1-GetDryRunResponse) | GetDryRun returns dry-run progress and produced signals. |
| ArchiveRule | [ArchiveRuleRequest](#psim-api-v1-ArchiveRuleRequest) | [ArchiveRuleResponse](#psim-api-v1-ArchiveRuleResponse) | ArchiveRule archives a rule; it is removed from the next rule set. |
| GetActiveRuleSet | [GetActiveRuleSetRequest](#psim-api-v1-GetActiveRuleSetRequest) | [GetActiveRuleSetResponse](#psim-api-v1-GetActiveRuleSetResponse) | GetActiveRuleSet returns the active rule set. |
| ActivateRuleSet | [ActivateRuleSetRequest](#psim-api-v1-ActivateRuleSetRequest) | [ActivateRuleSetResponse](#psim-api-v1-ActivateRuleSetResponse) | ActivateRuleSet atomically activates a new set of rule versions (RS1-RS3). |


<a name="psim-api-v1-MappingService"></a>

### MappingService
MappingService manages mapping rules of connector types. Owner: Normalizer. Scenario S1.

| Method Name | Request Type | Response Type | Description |
| ----------- | ------------ | ------------- | ------------|
| ListMappingRuleSets | [ListMappingRuleSetsRequest](#psim-api-v1-ListMappingRuleSetsRequest) | [ListMappingRuleSetsResponse](#psim-api-v1-ListMappingRuleSetsResponse) | ListMappingRuleSets lists the active mapping rule set of each connector type. |
| GetMappingRuleSet | [GetMappingRuleSetRequest](#psim-api-v1-GetMappingRuleSetRequest) | [GetMappingRuleSetResponse](#psim-api-v1-GetMappingRuleSetResponse) | GetMappingRuleSet returns the active mapping rule set of a connector type. |
| PublishMappingRuleSet | [PublishMappingRuleSetRequest](#psim-api-v1-PublishMappingRuleSetRequest) | [PublishMappingRuleSetResponse](#psim-api-v1-PublishMappingRuleSetResponse) | PublishMappingRuleSet validates and publishes a new version (M1-M3). |

 



<a name="psim_config_v1_config-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/config/v1/config.proto



<a name="psim-config-v1-ConfigRecord"></a>

### ConfigRecord
ConfigRecord is the value of compacted single-partition topic psim.config.v1 (key: config_key).
Each record is the full active object; consumers apply it atomically between input batches.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| rule_set | [psim.processing.v1.RuleSet](#psim-processing-v1-RuleSet) |  | Key &#34;ruleset/&lt;tenant_id&gt;&#34;. |
| mapping_rule_set | [psim.processing.v1.MappingRuleSet](#psim-processing-v1-MappingRuleSet) |  | Key &#34;mapping/&lt;tenant_id&gt;/&lt;connector_type&gt;&#34;. |
| retention | [RetentionSettings](#psim-config-v1-RetentionSettings) |  | Key &#34;retention/&lt;tenant_id&gt;&#34;. |
| processing | [ProcessingSettings](#psim-config-v1-ProcessingSettings) |  | Key &#34;processing/&lt;tenant_id&gt;&#34;. |






<a name="psim-config-v1-ProcessingSettings"></a>

### ProcessingSettings
ProcessingSettings are tenant-wide processing parameters.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| significance_threshold | [psim.common.v1.Severity](#psim-common-v1-Severity) |  | Events at or above this severity are significant (E3). |
| max_clock_skew | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| max_buffer_replay_age | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| source_idle_timeout | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| version | [uint64](#uint64) |  |  |






<a name="psim-config-v1-RetentionSettings"></a>

### RetentionSettings
RetentionSettings are data retention periods of a tenant.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| event_history_all | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| event_history_significant | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| incidents | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| audit | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| version | [uint64](#uint64) |  |  |





 

 

 

 



<a name="psim_api_v1_settings-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/api/v1/settings.proto



<a name="psim-api-v1-GetSettingsRequest"></a>

### GetSettingsRequest
GetSettingsRequest is the request of GetSettings.






<a name="psim-api-v1-GetSettingsResponse"></a>

### GetSettingsResponse
GetSettingsResponse is the response of GetSettings.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| retention | [psim.config.v1.RetentionSettings](#psim-config-v1-RetentionSettings) |  |  |
| processing | [psim.config.v1.ProcessingSettings](#psim-config-v1-ProcessingSettings) |  |  |






<a name="psim-api-v1-UpdateProcessingSettingsRequest"></a>

### UpdateProcessingSettingsRequest
UpdateProcessingSettingsRequest is the request of UpdateProcessingSettings.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| processing | [psim.config.v1.ProcessingSettings](#psim-config-v1-ProcessingSettings) |  |  |






<a name="psim-api-v1-UpdateProcessingSettingsResponse"></a>

### UpdateProcessingSettingsResponse
UpdateProcessingSettingsResponse is the response of UpdateProcessingSettings.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| processing | [psim.config.v1.ProcessingSettings](#psim-config-v1-ProcessingSettings) |  |  |






<a name="psim-api-v1-UpdateRetentionSettingsRequest"></a>

### UpdateRetentionSettingsRequest
UpdateRetentionSettingsRequest is the request of UpdateRetentionSettings.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| retention | [psim.config.v1.RetentionSettings](#psim-config-v1-RetentionSettings) |  |  |






<a name="psim-api-v1-UpdateRetentionSettingsResponse"></a>

### UpdateRetentionSettingsResponse
UpdateRetentionSettingsResponse is the response of UpdateRetentionSettings.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| retention | [psim.config.v1.RetentionSettings](#psim-config-v1-RetentionSettings) |  |  |





 

 

 


<a name="psim-api-v1-SettingsService"></a>

### SettingsService
SettingsService manages tenant-wide retention and processing settings.
Owner: Resource Catalog; changes are published to psim.config.v1.

| Method Name | Request Type | Response Type | Description |
| ----------- | ------------ | ------------- | ------------|
| GetSettings | [GetSettingsRequest](#psim-api-v1-GetSettingsRequest) | [GetSettingsResponse](#psim-api-v1-GetSettingsResponse) | GetSettings returns the current settings. |
| UpdateRetentionSettings | [UpdateRetentionSettingsRequest](#psim-api-v1-UpdateRetentionSettingsRequest) | [UpdateRetentionSettingsResponse](#psim-api-v1-UpdateRetentionSettingsResponse) | UpdateRetentionSettings replaces retention settings (If-Match required). |
| UpdateProcessingSettings | [UpdateProcessingSettingsRequest](#psim-api-v1-UpdateProcessingSettingsRequest) | [UpdateProcessingSettingsResponse](#psim-api-v1-UpdateProcessingSettingsResponse) | UpdateProcessingSettings replaces processing settings (If-Match required). |

 



<a name="psim_connector_v1_connector-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/connector/v1/connector.proto



<a name="psim-connector-v1-BatchAck"></a>

### BatchAck
BatchAck reports that a batch was durably written (acks=all) or rejected.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| batch_seq | [uint64](#uint64) |  |  |
| accepted | [uint32](#uint32) |  |  |
| rejections | [EventRejection](#psim-connector-v1-EventRejection) | repeated | Events rejected permanently; they must not be resent. |






<a name="psim-connector-v1-Command"></a>

### Command
Command asks the connector to act on a device. Execute at most once per command_id.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |
| device_external_id | [string](#string) |  |  |
| capability | [string](#string) |  |  |
| parameters | [Command.ParametersEntry](#psim-connector-v1-Command-ParametersEntry) | repeated |  |
| deadline_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-connector-v1-Command-ParametersEntry"></a>

### Command.ParametersEntry



| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-connector-v1-CommandAck"></a>

### CommandAck
CommandAck confirms that the connector received a command.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |






<a name="psim-connector-v1-CommandResult"></a>

### CommandResult
CommandResult reports the outcome of a command.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| command_id | [string](#string) |  |  |
| success | [bool](#bool) |  |  |
| code | [string](#string) |  | Error code when success is false. |
| message | [string](#string) |  |  |






<a name="psim-connector-v1-ConfigUpdate"></a>

### ConfigUpdate
ConfigUpdate sends the catalog configuration of the connector.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| config_version | [uint64](#uint64) |  |  |
| devices | [ConnectorDevice](#psim-connector-v1-ConnectorDevice) | repeated |  |






<a name="psim-connector-v1-ConnectorDevice"></a>

### ConnectorDevice
ConnectorDevice is a device served by the connector.


| Field | Type | Label | Description |
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
ConnectorEvent is one event from a source.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| event_id | [string](#string) |  | UUIDv7 assigned when the event was created; stable across resends. |
| source_ref | [string](#string) |  | Source reference known to the connector (Source.external_ref). |
| source_epoch | [uint32](#uint32) |  |  |
| source_seq | [uint64](#uint64) |  | Strictly increasing within source_ref and source_epoch. |
| occurred_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |
| raw_code | [string](#string) |  |  |
| attributes | [ConnectorEvent.AttributesEntry](#psim-connector-v1-ConnectorEvent-AttributesEntry) | repeated |  |
| traceparent | [string](#string) |  | W3C traceparent of the connector span, optional. |






<a name="psim-connector-v1-ConnectorEvent-AttributesEntry"></a>

### ConnectorEvent.AttributesEntry



| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-connector-v1-Credits"></a>

### Credits
Credits grants additional events to send.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| grant | [uint32](#uint32) |  |  |






<a name="psim-connector-v1-EventBatch"></a>

### EventBatch
EventBatch is a batch of events; the connector must not exceed granted credits.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| batch_seq | [uint64](#uint64) |  | Monotonic within the session; echoed in BatchAck. |
| events | [ConnectorEvent](#psim-connector-v1-ConnectorEvent) | repeated |  |






<a name="psim-connector-v1-EventRejection"></a>

### EventRejection
EventRejection is a permanent rejection of one event.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| event_id | [string](#string) |  |  |
| code | [string](#string) |  | Error code from the catalog, for example &#34;INGEST_INVALID_EVENT&#34;. |
| message | [string](#string) |  |  |






<a name="psim-connector-v1-Goodbye"></a>

### Goodbye
Goodbye closes the session gracefully.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| reason | [GoodbyeReason](#psim-connector-v1-GoodbyeReason) |  |  |
| message | [string](#string) |  |  |






<a name="psim-connector-v1-Heartbeat"></a>

### Heartbeat
Heartbeat keeps the session alive; sent by both sides.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| sent_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  |  |






<a name="psim-connector-v1-Register"></a>

### Register
Register opens the session.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| protocol_versions | [string](#string) | repeated | Protocol versions supported by the connector, for example [&#34;1.0&#34;]. |
| connector_version | [string](#string) |  |  |
| instance_id | [string](#string) |  | Unique per running connector process, for diagnostics. |
| config_version | [uint64](#uint64) |  | Catalog configuration version the connector already has. |






<a name="psim-connector-v1-Registered"></a>

### Registered
Registered confirms the session.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| session_id | [string](#string) |  |  |
| connector_id | [string](#string) |  |  |
| protocol_version | [string](#string) |  |  |
| initial_credits | [uint32](#uint32) |  | Number of events the connector may send before the next Credits. |
| heartbeat_interval | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| session_timeout | [google.protobuf.Duration](#google-protobuf-Duration) |  |  |
| max_batch_events | [uint32](#uint32) |  |  |
| max_batch_bytes | [uint32](#uint32) |  |  |






<a name="psim-connector-v1-SessionRequest"></a>

### SessionRequest
SessionRequest is a message from the connector to the gateway.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| register | [Register](#psim-connector-v1-Register) |  |  |
| heartbeat | [Heartbeat](#psim-connector-v1-Heartbeat) |  |  |
| event_batch | [EventBatch](#psim-connector-v1-EventBatch) |  |  |
| command_ack | [CommandAck](#psim-connector-v1-CommandAck) |  |  |
| command_result | [CommandResult](#psim-connector-v1-CommandResult) |  |  |
| goodbye | [Goodbye](#psim-connector-v1-Goodbye) |  |  |






<a name="psim-connector-v1-SessionResponse"></a>

### SessionResponse
SessionResponse is a message from the gateway to the connector.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| registered | [Registered](#psim-connector-v1-Registered) |  |  |
| heartbeat | [Heartbeat](#psim-connector-v1-Heartbeat) |  |  |
| batch_ack | [BatchAck](#psim-connector-v1-BatchAck) |  |  |
| credits | [Credits](#psim-connector-v1-Credits) |  |  |
| command | [Command](#psim-connector-v1-Command) |  |  |
| config_update | [ConfigUpdate](#psim-connector-v1-ConfigUpdate) |  |  |
| goodbye | [Goodbye](#psim-connector-v1-Goodbye) |  |  |





 


<a name="psim-connector-v1-GoodbyeReason"></a>

### GoodbyeReason
GoodbyeReason is the reason to close a session.

| Name | Number | Description |
| ---- | ------ | ----------- |
| GOODBYE_REASON_UNSPECIFIED | 0 |  |
| GOODBYE_REASON_SHUTDOWN | 1 |  |
| GOODBYE_REASON_DRAINING | 2 |  |
| GOODBYE_REASON_SUPERSEDED | 3 |  |
| GOODBYE_REASON_REVOKED | 4 |  |
| GOODBYE_REASON_PROTOCOL_ERROR | 5 |  |


 

 


<a name="psim-connector-v1-ConnectorGatewayService"></a>

### ConnectorGatewayService
ConnectorGatewayService is the connector protocol v1 (ADR-017).
Transport: gRPC over TLS 1.3 with mutual TLS; the client certificate identifies the connector.

| Method Name | Request Type | Response Type | Description |
| ----------- | ------------ | ------------- | ------------|
| Session | [SessionRequest](#psim-connector-v1-SessionRequest) stream | [SessionResponse](#psim-connector-v1-SessionResponse) stream | Session is the single bidirectional stream of a connector session. The first client message must be Register; the server answers Registered or closes the stream. |

 



<a name="psim_incident_v1_events-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/incident/v1/events.proto



<a name="psim-incident-v1-IncidentAcknowledged"></a>

### IncidentAcknowledged
IncidentAcknowledged is published when an operator accepts an incident.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-incident-v1-IncidentAssigned"></a>

### IncidentAssigned
IncidentAssigned is published on assignment or reassignment.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| previous_assignee_id | [string](#string) |  |  |






<a name="psim-incident-v1-IncidentBlockingStepsChanged"></a>

### IncidentBlockingStepsChanged
IncidentBlockingStepsChanged is published when blocking_steps changes from response events.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-incident-v1-IncidentClosed"></a>

### IncidentClosed
IncidentClosed is published on transition to closed.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-incident-v1-IncidentCommentAdded"></a>

### IncidentCommentAdded
IncidentCommentAdded is published when a comment is added.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| entry | [TimelineEntry](#psim-incident-v1-TimelineEntry) |  |  |






<a name="psim-incident-v1-IncidentCreated"></a>

### IncidentCreated
IncidentCreated is published when a signal opens a new incident.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| signal_id | [string](#string) |  |  |






<a name="psim-incident-v1-IncidentEscalated"></a>

### IncidentEscalated
IncidentEscalated is published when an escalation level is applied.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| sla_kind | [string](#string) |  | &#34;ack&#34; or &#34;resolve&#34;. |






<a name="psim-incident-v1-IncidentEventRecord"></a>

### IncidentEventRecord
IncidentEventRecord is the value of topic psim.incidents.events.v1 (key: incident_id).
Every event carries the incident state after the change; envelope.sequence = incident.version.


| Field | Type | Label | Description |
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






<a name="psim-incident-v1-IncidentLinkAdded"></a>

### IncidentLinkAdded
IncidentLinkAdded is published when an external link is attached.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| link | [Link](#psim-incident-v1-Link) |  |  |






<a name="psim-incident-v1-IncidentPriorityChanged"></a>

### IncidentPriorityChanged
IncidentPriorityChanged is published on computed or manual priority change.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| previous_priority | [psim.common.v1.Priority](#psim-common-v1-Priority) |  |  |






<a name="psim-incident-v1-IncidentReopened"></a>

### IncidentReopened
IncidentReopened is published on transition from resolved back to in_progress.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| reason | [string](#string) |  |  |






<a name="psim-incident-v1-IncidentResolved"></a>

### IncidentResolved
IncidentResolved is published on transition to resolved.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-incident-v1-IncidentWorkStarted"></a>

### IncidentWorkStarted
IncidentWorkStarted is published on transition to in_progress.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |






<a name="psim-incident-v1-SignalAttached"></a>

### SignalAttached
SignalAttached is published when a signal joins an open incident.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident | [Incident](#psim-incident-v1-Incident) |  |  |
| signal_id | [string](#string) |  |  |





 

 

 

 



<a name="psim_ingest_v1_raw_event-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/ingest/v1/raw_event.proto



<a name="psim-ingest-v1-RawEvent"></a>

### RawEvent
RawEvent is an event as delivered by a connector, before normalization.
envelope.message_id is the event_id assigned by the Connector SDK,
envelope.sequence is the source sequence number, envelope.occurred_at is the event time.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| connector_id | [string](#string) |  |  |
| source_id | [string](#string) |  | Deterministic id: UUIDv5(connector_id, source_ref), see ADR-029. |
| source_ref | [string](#string) |  | Source reference as known to the connector (Source.external_ref in the catalog). |
| source_epoch | [uint32](#uint32) |  | Incremented when the source resets its sequence counter. |
| raw_code | [string](#string) |  | Vendor event code. |
| attributes | [RawEvent.AttributesEntry](#psim-ingest-v1-RawEvent-AttributesEntry) | repeated |  |
| clock_skew_corrected | [bool](#bool) |  | Set when occurred_at was replaced with received_at because of clock skew (ADR-010). |
| source_occurred_at | [google.protobuf.Timestamp](#google-protobuf-Timestamp) |  | Original source timestamp when clock_skew_corrected is set. |






<a name="psim-ingest-v1-RawEvent-AttributesEntry"></a>

### RawEvent.AttributesEntry



| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| key | [string](#string) |  |  |
| value | [string](#string) |  |  |






<a name="psim-ingest-v1-RawEventRecord"></a>

### RawEventRecord
RawEventRecord is the value of topic psim.ingest.raw.v1 (key: source_id).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| raw_event | [RawEvent](#psim-ingest-v1-RawEvent) |  |  |





 

 

 

 



<a name="psim_processing_v1_internal-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/processing/v1/internal.proto



<a name="psim-processing-v1-CorrelationChangelogRecord"></a>

### CorrelationChangelogRecord
CorrelationChangelogRecord is the value of internal compacted topic
psim.correlation.changelog.v1 (key: rule_id:correlation_key). Not a public contract.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| state | [CorrelationStateEntry](#psim-processing-v1-CorrelationStateEntry) |  |  |






<a name="psim-processing-v1-CorrelationStateEntry"></a>

### CorrelationStateEntry
CorrelationStateEntry is the opaque state of one rule for one correlation key.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| rule_id | [string](#string) |  |  |
| rule_version | [uint32](#uint32) |  |  |
| correlation_key | [string](#string) |  |  |
| state | [bytes](#bytes) |  | Engine-defined encoding, versioned by state_format. |
| state_format | [uint32](#uint32) |  |  |






<a name="psim-processing-v1-NormalizerStateRecord"></a>

### NormalizerStateRecord
NormalizerStateRecord is the value of internal compacted topic psim.normalizer.state.v1
(key: source_id:epoch). Not a public contract.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| source_state | [SourceSequenceState](#psim-processing-v1-SourceSequenceState) |  |  |






<a name="psim-processing-v1-RepartitionRecord"></a>

### RepartitionRecord
RepartitionRecord is the value of internal topic psim.correlation.repartition.v1
(key: correlation_key). Not a public contract.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| repartitioned_event | [RepartitionedEvent](#psim-processing-v1-RepartitionedEvent) |  |  |






<a name="psim-processing-v1-RepartitionedEvent"></a>

### RepartitionedEvent
RepartitionedEvent carries an event already filtered by a rule wider than a zone.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| rule_id | [string](#string) |  |  |
| rule_version | [uint32](#uint32) |  |  |
| correlation_key | [string](#string) |  |  |
| event | [Event](#psim-processing-v1-Event) |  |  |






<a name="psim-processing-v1-SourceSequenceState"></a>

### SourceSequenceState
SourceSequenceState is the deduplication tracker of one source epoch (data-flows.md, 3.2).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| source_id | [string](#string) |  |  |
| source_epoch | [uint32](#uint32) |  |  |
| max_sequence | [uint64](#uint64) |  |  |
| window | [bytes](#bytes) |  | Bitmap of the last 4096 sequence numbers below max_sequence; bit i set = max_sequence - i seen. |





 

 

 

 



<a name="psim_response_v1_events-proto"></a>
<p align="right"><a href="#top">Top</a></p>

## psim/response/v1/events.proto



<a name="psim-response-v1-CommandUpdated"></a>

### CommandUpdated
CommandUpdated reports a change of a command; the change equals the new state.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| command | [Command](#psim-response-v1-Command) |  |  |






<a name="psim-response-v1-EscalationLevelTriggered"></a>

### EscalationLevelTriggered
EscalationLevelTriggered reports that an escalation level fired for an incident.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| level | [uint32](#uint32) |  |  |
| policy_id | [string](#string) |  |  |
| notification_ids | [string](#string) | repeated |  |






<a name="psim-response-v1-NotificationUpdated"></a>

### NotificationUpdated
NotificationUpdated reports a change of a notification; the change equals the new state.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| notification | [Notification](#psim-response-v1-Notification) |  |  |






<a name="psim-response-v1-ResponseEventRecord"></a>

### ResponseEventRecord
ResponseEventRecord is the value of topic psim.response.events.v1
(key: incident_id; command_id or notification_id when there is no incident).
Events carry the aggregate state after the change; envelope.sequence = aggregate version.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| envelope | [psim.common.v1.Envelope](#psim-common-v1-Envelope) |  |  |
| run_updated | [RunUpdated](#psim-response-v1-RunUpdated) |  |  |
| command_updated | [CommandUpdated](#psim-response-v1-CommandUpdated) |  |  |
| notification_updated | [NotificationUpdated](#psim-response-v1-NotificationUpdated) |  |  |
| sla_breached | [SlaBreached](#psim-response-v1-SlaBreached) |  |  |
| escalation_level_triggered | [EscalationLevelTriggered](#psim-response-v1-EscalationLevelTriggered) |  |  |






<a name="psim-response-v1-RunUpdated"></a>

### RunUpdated
RunUpdated reports a change of a response run.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| change | [RunChange](#psim-response-v1-RunChange) |  |  |
| step_id | [string](#string) |  | Step affected by the change, if any. |
| run | [ResponseRun](#psim-response-v1-ResponseRun) |  |  |






<a name="psim-response-v1-SlaBreached"></a>

### SlaBreached
SlaBreached reports that an incident missed its SLA.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| incident_id | [string](#string) |  |  |
| sla_kind | [string](#string) |  | &#34;ack&#34; or &#34;resolve&#34;. |
| policy_id | [string](#string) |  |  |





 


<a name="psim-response-v1-RunChange"></a>

### RunChange
RunChange maps to domain events ResponseRunStarted, StepActivated, StepCompleted, StepSkipped,
StepFailed, MandatoryStepsChanged, ResponseRunCompleted, ResponseRunCancelled.

| Name | Number | Description |
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
<p align="right"><a href="#top">Top</a></p>

## psim/realtime/v1/realtime.proto



<a name="psim-realtime-v1-Authenticate"></a>

### Authenticate
Authenticate authenticates the connection; also sent on token refresh.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| access_token | [string](#string) |  |  |






<a name="psim-realtime-v1-Authenticated"></a>

### Authenticated
Authenticated confirms authentication.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| user_id | [string](#string) |  |  |
| expires_in | [uint32](#uint32) |  | Seconds until the token expires; the client must re-authenticate before. |






<a name="psim-realtime-v1-ClientFrame"></a>

### ClientFrame
ClientFrame is a frame sent by the browser.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| request_id | [string](#string) |  | Client-chosen id echoed in replies. |
| authenticate | [Authenticate](#psim-realtime-v1-Authenticate) |  |  |
| subscribe | [Subscribe](#psim-realtime-v1-Subscribe) |  |  |
| unsubscribe | [Unsubscribe](#psim-realtime-v1-Unsubscribe) |  |  |
| ping | [Ping](#psim-realtime-v1-Ping) |  |  |






<a name="psim-realtime-v1-Delta"></a>

### Delta
Delta is one change; apply only if the aggregate version is newer than known.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| subscription_id | [string](#string) |  |  |
| resume_token | [string](#string) |  | Token to resume after this delta. |
| incident_event | [psim.incident.v1.IncidentEventRecord](#psim-incident-v1-IncidentEventRecord) |  |  |
| response_event | [psim.response.v1.ResponseEventRecord](#psim-response-v1-ResponseEventRecord) |  |  |






<a name="psim-realtime-v1-ErrorFrame"></a>

### ErrorFrame
ErrorFrame reports an error; the connection is closed after fatal errors.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| code | [string](#string) |  | Error code from the catalog, for example &#34;REALTIME_SLOW_CONSUMER&#34;. |
| message | [string](#string) |  |  |
| fatal | [bool](#bool) |  |  |






<a name="psim-realtime-v1-FeedFilter"></a>

### FeedFilter
FeedFilter narrows a subscription; the access scope is always applied by the server.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| site_ids | [string](#string) | repeated |  |
| zone_ids | [string](#string) | repeated |  |
| incident_id | [string](#string) |  |  |
| include_closed | [bool](#bool) |  |  |






<a name="psim-realtime-v1-Ping"></a>

### Ping
Ping checks the connection.






<a name="psim-realtime-v1-Pong"></a>

### Pong
Pong answers Ping.






<a name="psim-realtime-v1-ResyncRequired"></a>

### ResyncRequired
ResyncRequired tells the client to take a new snapshot (gap too long or token expired).


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| subscription_id | [string](#string) |  |  |
| reason | [string](#string) |  |  |






<a name="psim-realtime-v1-ServerFrame"></a>

### ServerFrame
ServerFrame is a frame sent by the server.


| Field | Type | Label | Description |
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
Subscribe opens a subscription from a resume token.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| subscription_id | [string](#string) |  |  |
| stream | [Stream](#psim-realtime-v1-Stream) |  |  |
| filter | [FeedFilter](#psim-realtime-v1-FeedFilter) |  |  |
| resume_token | [string](#string) |  | Token from a snapshot (GET /v1/feed/incidents) or from the last received Delta. |






<a name="psim-realtime-v1-Subscribed"></a>

### Subscribed
Subscribed confirms a subscription.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| subscription_id | [string](#string) |  |  |
| resume_token | [string](#string) |  |  |






<a name="psim-realtime-v1-Unsubscribe"></a>

### Unsubscribe
Unsubscribe closes a subscription.


| Field | Type | Label | Description |
| ----- | ---- | ----- | ----------- |
| subscription_id | [string](#string) |  |  |





 


<a name="psim-realtime-v1-Stream"></a>

### Stream
Stream selects what to receive.

| Name | Number | Description |
| ---- | ------ | ----------- |
| STREAM_UNSPECIFIED | 0 |  |
| STREAM_INCIDENTS | 1 | Incident and response changes within the user scope. |
| STREAM_INCIDENT | 2 | Changes of one incident (filter.incident_id required). |


 

 

 



## Scalar Value Types

| .proto Type | Notes | C++ | Java | Python | Go | C# | PHP | Ruby |
| ----------- | ----- | --- | ---- | ------ | -- | -- | --- | ---- |
| <a name="double" /> double |  | double | double | float | float64 | double | float | Float |
| <a name="float" /> float |  | float | float | float | float32 | float | float | Float |
| <a name="int32" /> int32 | Uses variable-length encoding. Inefficient for encoding negative numbers – if your field is likely to have negative values, use sint32 instead. | int32 | int | int | int32 | int | integer | Bignum or Fixnum (as required) |
| <a name="int64" /> int64 | Uses variable-length encoding. Inefficient for encoding negative numbers – if your field is likely to have negative values, use sint64 instead. | int64 | long | int/long | int64 | long | integer/string | Bignum |
| <a name="uint32" /> uint32 | Uses variable-length encoding. | uint32 | int | int/long | uint32 | uint | integer | Bignum or Fixnum (as required) |
| <a name="uint64" /> uint64 | Uses variable-length encoding. | uint64 | long | int/long | uint64 | ulong | integer/string | Bignum or Fixnum (as required) |
| <a name="sint32" /> sint32 | Uses variable-length encoding. Signed int value. These more efficiently encode negative numbers than regular int32s. | int32 | int | int | int32 | int | integer | Bignum or Fixnum (as required) |
| <a name="sint64" /> sint64 | Uses variable-length encoding. Signed int value. These more efficiently encode negative numbers than regular int64s. | int64 | long | int/long | int64 | long | integer/string | Bignum |
| <a name="fixed32" /> fixed32 | Always four bytes. More efficient than uint32 if values are often greater than 2^28. | uint32 | int | int | uint32 | uint | integer | Bignum or Fixnum (as required) |
| <a name="fixed64" /> fixed64 | Always eight bytes. More efficient than uint64 if values are often greater than 2^56. | uint64 | long | int/long | uint64 | ulong | integer/string | Bignum |
| <a name="sfixed32" /> sfixed32 | Always four bytes. | int32 | int | int | int32 | int | integer | Bignum or Fixnum (as required) |
| <a name="sfixed64" /> sfixed64 | Always eight bytes. | int64 | long | int/long | int64 | long | integer/string | Bignum |
| <a name="bool" /> bool |  | bool | boolean | boolean | bool | bool | boolean | TrueClass/FalseClass |
| <a name="string" /> string | A string must always contain UTF-8 encoded or 7-bit ASCII text. | string | String | str/unicode | string | string | string | String (UTF-8) |
| <a name="bytes" /> bytes | May contain any arbitrary sequence of bytes. | string | ByteString | str | []byte | ByteString | string | String (ASCII-8BIT) |

