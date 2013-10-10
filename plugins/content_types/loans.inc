<?php

/**
 * Plugins are described by creating a $plugin array which will be used
 * by the system that includes this file.
 */
$plugin = array(
  'title' => t('User loan list'),
  'description' => t('The current loans for a user'),
  'single' => TRUE,
  'content_types' => array('list'),
  'required context' => new ctools_context_required(t('User'), 'user'),
  'category' => t('User'),
);

/**
 * Render the block.
 */
function ding_loan_loans_content_type_render($subtype, $conf, $panel_args, $context) {
  // Define basic block object.
  $block = new stdClass();
  $block->module = 'ding_loan';
  $block->delta = 'loans';
  $block->title = t('Loan list');
  $block->content = t('You do not currently have any loans.');

  // Get loans from the provider.
  $account = isset($context->data) ? $context->data : NULL;

  $loans = FALSE;
  if (module_exists('ding_session_cache')) {
    $loans = ding_session_cache_get('ding_loan', 'list');
  }

  if (!$loans) {
    $loans = ding_provider_invoke_page('loan', 'list', $account);

    // Pre-load all entities to speed up the page.
    $ids = array();
    foreach ($loans as $item) {
      $ids[] = $item->ding_entity_id;
    }
    ding_entity_load_multiple($ids);

    // Add the pre-load entity to each loan, if the entity exists.
    foreach ($loans as $id => &$item) {
      $item->entity;
    }

    // Store the loans into ding session cache.
    if (module_exists('ding_session_cache')) {
      ding_session_cache_set('ding_loan', 'list', $loans);
    }
  }

  // Set block content, with loans form if any loans exists.
  if (!empty($loans)) {
    $block->content = ding_provider_get_form('ding_loan_loans_form', $account, $loans);
  }

  return $block;
}

/**
 * Adding the admin form, to be able to control the required context.
 */
function ding_loan_loans_content_type_edit_form($form, &$form_state) {
  return $form;
}
