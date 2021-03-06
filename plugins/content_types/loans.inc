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

/**
 * Implements a drupal form, which show a list of reservations.
 */
function ding_loan_loans_form($form, &$form_state, $account, $loans = array()) {
  // Sort loans base on expire date.
  uasort($loans, 'ding_loan_sort_expiry');

  // Split the loans into groups based on the expiry date.
  $groups = _ding_loan_loans_group_loans_by_date($loans);

  // Store account information in the form (on server site).
  $form['account'] = array(
    '#type' => 'value',
    '#value' => $account,
  );

  // Store the loan list in the form (on server site).
  $form['items'] = array(
    '#type' => 'value',
    '#value' => $loans,
  );

  foreach ($groups as $gid => $group) {
    // Counter to ensure that the "select all" checkbox is disabled if all loans
    // in group are disabled.
    $disabled_count = 0;

    foreach ($group['loans'] as $loan) {

      // Add title item (grouping by due dates).
      $form['loans'][$gid]['title'] = array(
        '#type' => 'checkbox',
        '#title' => $group['title'],
        '#prefix' => '<div class="select-all ' . $gid . '">',
        '#suffix' => '</div>',
        '#attached' => array(
          'js' => array(
            drupal_get_path('module', 'ding_loan') . '/js/ding_loan.js',
          ),
        ),
      );

      // Try to get entity for the loan.
      $entity = $loan->entity;
      if (!is_object($entity)) {
        $entity = ding_provider_get_pseudo_entity($loan->ding_entity_id);
      }

      // Create title that links to the object.
      $uri = entity_uri('ting_object', $entity);
      $title = l($entity->getTitle() ? $entity->getTitle() : $loan->display_name, $uri['path']);

      $item = array(
        '#type' => 'material_item',
        '#id' => $loan->id,
        '#title' => $title,
        '#cover' => field_view_field('ting_object', $entity, 'ting_cover', 'user_list'),
        '#information' => array(
          'loan_date' => array(
            'label' => t('Loan date'),
            'data' => format_date(strtotime(check_plain($loan->loan_date)), 'custom', 'd/m/Y'),
            'class' => 'loan-date',
            '#weight' => 0,
          ),
          'expiry' => array(
            'label' => t('Return date'),
            'data' => format_date(strtotime(check_plain($loan->expiry)), 'custom', 'd/m/Y'),
            'class' => 'expire-date',
            '#weight' => 4,
          ),
        ),
        '#disabled' => !$loan->renewable,
      );

      // Add material number.
      if (isset($loan->materials_number)) {
        $item['#information']['material_number'] = array(
          'label' => t('Material no.'),
          'data' => $loan->materials_number,
          'class' => 'material-number',
          '#weight' => 8,
        );
      }

      // If not renewable set message.
      if (empty($loan->renewable)) {
        $disabled_count++;
        $item['#material_message'] = array(
          'message' => t('This material can not be re-newed'),
          'class' => 'messages warning',
        );
      }

      // Add the reservation to the form.
      $form['loans'][$gid][$loan->id] = $item;
    }

    // Check if the "Select all" for this group should be disabled.
    if (count($group['loans']) == $disabled_count) {
      $form['loans'][$gid]['title']['#disabled'] = TRUE;
    }
  }

  // Add the renew buttons to top and bottom of the form.
  $form['actions_top'] = array(
    '#prefix' => '<div class="action-buttons">',
    '#suffix' => '</div>',
    '#weight' => 0,
  );

  $form['actions_top']['submit_first'] = array(
    '#type' => 'submit',
    '#prefix' => '<div class="renew-loan-button action-button">',
    '#value' => t('Renew selected (@count)', array('@count' => 0)),
    '#suffix' => '</div>',
  );

  return $form;
}

/**
 * Submit handler for the form.
 */
function ding_loan_loans_form_submit($form, &$form_state) {
  $ids = array();
  foreach ($form_state['values']['items'] as $item) {
    if ($item->renewable && $form_state['values'][$item->id]) {
      $ids[] = $item->id;
    }
  }

  $clear_cache = FALSE;
  if (!empty($ids)) {
    $result = ding_provider_invoke('loan', 'renew', $form_state['values']['account'], $ids);
    foreach ($result as $id => $status) {
      if ($object = $form_state['values']['items'][$id]->entity) {
        $title = $object->title;
      }
      else {
        $title = $item->display_name;
      }
      // @todo Better feedback.
      switch ($status) {
        case DingProviderLoan::STATUS_RENEWED:
          drupal_set_message(t('"@title renewed"', array('@title' => $title)));
          $clear_cache = TRUE;
          break;

        case DingProviderLoan::STATUS_NOT_RENEWED:
          if ($status['message'] == 'maxNofRenewals') {
            $message = t('Maximum number of renewals reached');
          }
          elseif ($status['message'] == 'copyIsReserved') {
            $message = t('The material is reserved by another loaner');
          }
          else {
            $message = t('Unable to renew material');
          }
          drupal_set_message(t('"@title not renewed: @message"', array('@title' => $title, '@message' => $message)), 'warning');
          break;

        default:
          watchdog('ding_loan', 'Unknown renewal status code @code.', array('@code' => $status), WATCHDOG_ERROR);
      }
    }

    // Clear session cache.
    if ($clear_cache && module_exists('ding_session_cache')) {
      ding_session_cache_clear('ding_loan', 'list');
    }
  }
}

/**
 * Renew load submit handler.
 */
function ding_loan_loans_form_renew_submit($form, &$form_state) {
  form_set_value($form['loans'], array($form_state['triggering_element']['#loan_id']), $form_state);
  ding_loan_loans_form_submit($form, $form_state);
}

/**
 * Helper function to group loans by their due dates.
 */
function _ding_loan_loans_group_loans_by_date($loans) {
  $groups = array();
  $today = date('Y-m-d', REQUEST_TIME);

  foreach ($loans as $loan) {
    if ($loan->expiry < $today) {
      // Loans that are overdue.
      if (!isset($groups['overdue'])) {
        $groups['overdue'] = array(
          'title' => t('Loans overdue'),
          'loans' => array(),
        );
      }
      $groups['overdue']['loans'][$loan->id] = $loan;
    }
    elseif ($loan->expiry == $today) {
      // Loans that should be handed in today.
      if (!isset($groups['due'])) {
        $groups['due'] = array(
          'title' => t('Loans due'),
          'loans' => array(),
        );
      }
      $groups['due']['loans'][$loan->id] = $loan;
    }
    else {
      // Loans that are to be handed in in x days.
      if (!isset($groups[$loan->expiry])) {
        $time = strtotime($loan->expiry);
        $due_in = t('Due in @count, @date', array(
          '@count' => format_interval($time - time(), 1),
          '@date' => date('d/m/y', $time),
        ));
        $groups[$loan->expiry] = array(
          'title' => $due_in,
          'loans' => array(),
        );
      }
      $groups[$loan->expiry]['loans'][$loan->id] = $loan;
    }
  }

  return $groups;
}
